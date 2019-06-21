#include <random>
#include "ut_helper.h"
#define protected public
#define private public
#include "orch.h"
#include "orchdaemon.h"
#include "vnetorch.h"
#include "vxlanorch.h"
#undef protected
#undef private
#include "saiattributelist.h"
#include "saihelper.h"
#include "swssnet.h"
#include "tokenize.h"
#include "subscriberstatetable.h"

#define  _dbg_ printf("%s %d \r\n",__FUNCTION__,__LINE__);

using namespace std;
using namespace swss;

extern void syncd_apply_view();

/* Global variables */
extern sai_object_id_t gVirtualRouterId;
extern sai_object_id_t gUnderlayIfId;
extern sai_object_id_t gSwitchId;
extern sai_object_id_t gVlanId;
extern vector<sai_object_id_t> gAllPorts;
extern MacAddress gMacAddress;
extern MacAddress gVxlanMacAddress;

extern int gBatchSize;

#define EXPECT_PORT_COUNT   32
#define CPU_MAC             "00:01:02:03:04:05"


extern bool gSairedisRecord;
extern bool gSwssRecord;
extern bool gLogRotate;
extern ofstream gRecordOfs;
extern string gRecordFile;

extern sai_switch_api_t*            sai_switch_api;
extern sai_qos_map_api_t*           sai_qos_map_api;
extern sai_wred_api_t*              sai_wred_api;
extern sai_port_api_t*              sai_port_api;
extern sai_vlan_api_t*              sai_vlan_api;
extern sai_bridge_api_t*            sai_bridge_api;
extern sai_virtual_router_api_t*    sai_virtual_router_api;
extern sai_router_interface_api_t*  sai_router_intfs_api;
extern sai_tunnel_api_t*            sai_tunnel_api;
extern sai_next_hop_api_t*          sai_next_hop_api;

extern PortsOrch*       gPortsOrch;
extern BufferOrch*      gBufferOrch;
extern IntfsOrch*       gIntfsOrch;

extern Directory<Orch*> gDirectory;


namespace VnetOrchTest
{
    using namespace testing;

    shared_ptr<DBConnector> m_configDb;
    shared_ptr<DBConnector> m_applDb;
    shared_ptr<DBConnector> m_stateDb;

    class OrchagentStub
    {
    public:
        sai_status_t saiInit()
        {
            // Init switch and create dependencies

            map<string, string> profile = {
                { "SAI_VS_SWITCH_TYPE", "SAI_VS_SWITCH_TYPE_BCM56850" },
                { "KV_DEVICE_MAC_ADDRESS", "20:03:04:05:06:00" }
            };

            auto status = ut_helper::initSaiApi(profile);
            if(status != SAI_STATUS_SUCCESS) {
                return status;
            }

            sai_attribute_t attr;

            attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
            attr.value.booldata = true;

            status = sai_switch_api->create_switch(&gSwitchId, 1, &attr);
            if(status != SAI_STATUS_SUCCESS) {
                return status;
            }

            /* Get the default virtual router ID */
            attr.id = SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
            if(status != SAI_STATUS_SUCCESS) {
                return status;
            }
            gVirtualRouterId = attr.value.oid;

            /* Create a loopback underlay router interface */
            vector<sai_attribute_t> underlay_intf_attrs;
            sai_attribute_t underlay_intf_attr;
            underlay_intf_attr.id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
            underlay_intf_attr.value.oid = gVirtualRouterId;
            underlay_intf_attrs.push_back(underlay_intf_attr);
            underlay_intf_attr.id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
            underlay_intf_attr.value.s32 = SAI_ROUTER_INTERFACE_TYPE_LOOPBACK;
            underlay_intf_attrs.push_back(underlay_intf_attr);

            status = sai_router_intfs_api->create_router_interface(&gUnderlayIfId, gSwitchId, (uint32_t)underlay_intf_attrs.size(), underlay_intf_attrs.data());
            if(status != SAI_STATUS_SUCCESS) {
                return status;
            }

            return status;
        }

        sai_status_t saiUnInit()
        {
            auto status = sai_switch_api->remove_switch(gSwitchId);
            gSwitchId = 0;

            ut_helper::uninitSaiApi();
            return status;
        }

    };

    OrchagentStub orchgent_stub;
    
    struct VnetTestBase : public ::testing::Test {
        vector<int32_t*> m_s32list_pool;
    
        virtual ~VnetTestBase()
        {
            for (auto p : m_s32list_pool) {
                free(p);
            }
        }
    };

    class VnetOrchTest : public VnetTestBase
    {
    public:
        VNetOrch *vnet_orch;
        VxlanTunnelOrch *vxlan_tunnel_orch;
        VRFOrch *vrf_orch ;
        VNetRouteOrch *vnet_rt_orch;
        string platform = "";

        VnetOrchTest()
        {
            m_configDb = make_shared<DBConnector>(CONFIG_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_applDb = make_shared<DBConnector>(APPL_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_stateDb = make_shared<DBConnector>(STATE_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
        }

        ~VnetOrchTest()
        {
        }

        void SetUp()
        {
            ASSERT_TRUE(orchgent_stub.saiInit() == SAI_STATUS_SUCCESS);
            const int portsorch_base_pri = 40;
            
            vector<table_name_with_pri_t> ports_tables = {
                { APP_PORT_TABLE_NAME,        portsorch_base_pri + 5 },
                { APP_VLAN_TABLE_NAME,        portsorch_base_pri + 2 },
                { APP_VLAN_MEMBER_TABLE_NAME, portsorch_base_pri     },
                { APP_LAG_TABLE_NAME,         portsorch_base_pri + 4 },
                { APP_LAG_MEMBER_TABLE_NAME,  portsorch_base_pri     }
            };
            
            gCrmOrch = new CrmOrch(m_configDb.get(), CFG_CRM_TABLE_NAME);
            gPortsOrch = new PortsOrch(m_applDb.get(), ports_tables);

            vector<string> vnet_tables = {
                APP_VNET_RT_TABLE_NAME,
                APP_VNET_RT_TUNNEL_TABLE_NAME
            };
            
            if(!gDirectory.get<VNetOrch*>()){
                if (platform == MLNX_PLATFORM_SUBSTRING)
                {
                    vnet_orch = new VNetOrch(m_applDb.get(), APP_VNET_TABLE_NAME, VNET_EXEC::VNET_EXEC_BRIDGE);
                }
                else
                {
                    vnet_orch = new VNetOrch(m_applDb.get(), APP_VNET_TABLE_NAME);
                }
                gDirectory.set(vnet_orch);
            }else{
                vnet_orch = gDirectory.get<VNetOrch*>();
            }
            
            if(!gDirectory.get<VNetRouteOrch*>()){
                vnet_rt_orch = new VNetRouteOrch(m_applDb.get(), vnet_tables, vnet_orch);
                gDirectory.set(vnet_rt_orch);
            }else{
                vnet_rt_orch = gDirectory.get<VNetRouteOrch*>();
            }
                        
            if(!gDirectory.get<VRFOrch*>()){
                vrf_orch = new VRFOrch(m_applDb.get(), APP_VRF_TABLE_NAME);
                gDirectory.set(vrf_orch);
            }else{
                vrf_orch = gDirectory.get<VRFOrch*>();
            }
            
            gIntfsOrch = new IntfsOrch(m_applDb.get(), APP_INTF_TABLE_NAME, vrf_orch);

            if(!gDirectory.get<VxlanTunnelOrch*>()){
                vxlan_tunnel_orch = new VxlanTunnelOrch(m_applDb.get(), APP_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
            }else{
                vxlan_tunnel_orch = gDirectory.get<VxlanTunnelOrch*>();
            }

            vector<string> buffer_tables = {
                CFG_BUFFER_POOL_TABLE_NAME,
                CFG_BUFFER_PROFILE_TABLE_NAME,
                CFG_BUFFER_QUEUE_TABLE_NAME,
                CFG_BUFFER_PG_TABLE_NAME,
                CFG_BUFFER_PORT_INGRESS_PROFILE_LIST_NAME,
                CFG_BUFFER_PORT_EGRESS_PROFILE_LIST_NAME
            };
            gBufferOrch = new BufferOrch(m_configDb.get(), buffer_tables);

            port_table_init();

        }

        void TearDown()
        {
            delete gPortsOrch;
            delete gIntfsOrch;
            //delete vxlan_tunnel_orch;
            //delete vrf_orch;
            //delete vnet_rt_orch;
            //delete vnet_orch;            
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
        }

        static bool AttrListEq_for_two_sai_attr(sai_object_type_t objecttype, const std::vector<sai_attribute_t>& act_attr_list, const std::vector<sai_attribute_t>& exp_attr_list)
        {
            if (act_attr_list.size() != exp_attr_list.size()) {
                return false;
            }
        
            for (uint32_t i = 0; i < exp_attr_list.size(); ++i) {
                sai_attr_id_t id = exp_attr_list[i].id;
                auto meta = sai_metadata_get_attr_metadata(objecttype, id);
        
                assert(meta != nullptr);
        
                // The following id can not serialize, check id only
                if (id == SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE || id == SAI_ACL_BIND_POINT_TYPE_PORT || id == SAI_ACL_BIND_POINT_TYPE_LAG) {
                    if (id != act_attr_list[i].id) {
                        auto meta_act = sai_metadata_get_attr_metadata(objecttype, act_attr_list[i].id);
        
                        // TODO: Show only on debug mode
                        if (meta_act) {
                            std::cerr << "AttrListEq failed\n";
                            std::cerr << "Actual:   " << meta_act->attridname << "\n";
                            std::cerr << "Expected: " << meta->attridname << "\n";
                        }
                    }
        
                    continue;
                }
        
                auto act_str = sai_serialize_attr_value(*meta, act_attr_list[i]);
                auto exp_str = sai_serialize_attr_value(*meta, exp_attr_list[i]);
                // TODO: Show only on debug mode
                if (act_str != exp_str) {
                    std::cerr << "i:        "<<i<<"\n";
                    std::cerr << "AttrListEq failed\n";
                    std::cerr << "Actual:   " << act_str << "\n";
                    std::cerr << "Expected: " << exp_str << "\n";
                    return false;
                }
            }
        
            return true;
        }

        void port_table_init(void)
        {
            sai_attribute_t attr;
            sai_status_t status;
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_PORT_TABLE_NAME, 1, 1), gPortsOrch, APP_PORT_TABLE_NAME));

            /* Get port number */
            auto port_count = attr.value.u32;
            attr.id = SAI_SWITCH_ATTR_PORT_NUMBER;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            
            port_count = attr.value.u32;
            
            /* Get port list */
            vector<sai_object_id_t> portOids;
            portOids.resize(port_count);
            attr.id = SAI_SWITCH_ATTR_PORT_LIST;
            attr.value.objlist.count = (uint32_t)portOids.size();
            attr.value.objlist.list = portOids.data();
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            
            deque<KeyOpFieldsValuesTuple> port_init_tuple;
            for (auto i = 0; i < port_count; i++) {
                vector<sai_uint32_t> lanes(8, 0);
                attr.id = SAI_PORT_ATTR_HW_LANE_LIST;
                attr.value.u32list.count = lanes.size();
                attr.value.u32list.list = lanes.data();
            
                status = sai_port_api->get_port_attribute(portOids[i], 1, &attr);
                ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            
                ostringstream lan_map_oss;
                lanes.resize(attr.value.u32list.count);
                if (!lanes.empty()) {
                    copy(lanes.begin(), lanes.end() - 1, ostream_iterator<sai_uint32_t>(lan_map_oss, ","));
                    lan_map_oss << lanes.back();
                }
            
                port_init_tuple.push_back(
                    { "Ethernet" + to_string(i), SET_COMMAND, { { "lanes", lan_map_oss.str() }} });
            }
            
            port_init_tuple.push_back({ "PortConfigDone", SET_COMMAND, { { "count", to_string(port_count) } } });
            port_init_tuple.push_back({ "PortInitDone", EMPTY_PREFIX, { { "", "" } } });
            Portal::ConsumerInternal::addToSync(consumer.get(), port_init_tuple);
            
            static_cast<Orch*>(gPortsOrch)->doTask(*consumer.get());
            ASSERT_TRUE(gPortsOrch->isInitDone());

        }

        bool validate_vxlan_tunnel(sai_object_type_t objecttype, vector<sai_attribute_t> exp_attrlist, sai_object_id_t object_id, sai_route_entry_t *route_entry = nullptr)
        {        
            vector<sai_attribute_t> act_attr;
            sai_status_t status;

            for (uint32_t i = 0; i < exp_attrlist.size(); ++i) {
                const auto attr = exp_attrlist[i];
                auto meta = sai_metadata_get_attr_metadata(objecttype, attr.id);
    
                if (meta == nullptr) {
                    return false;
                }
    
                sai_attribute_t new_attr;
                memset(&new_attr, 0, sizeof(new_attr));
                new_attr.id = attr.id;
                new_attr.id = attr.id;
                if( meta->attrvaluetype == SAI_ATTR_VALUE_TYPE_OBJECT_LIST){
                    new_attr.value.objlist.list = (sai_object_id_t*)malloc(sizeof(sai_object_id_t) * attr.value.objlist.count);
                    new_attr.value.objlist.count = attr.value.objlist.count;
                }
                
                new_attr.id = attr.id;
                act_attr.emplace_back(new_attr);
            }

            switch(objecttype)
            {
                case SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY:
                    status = sai_tunnel_api->get_tunnel_map_entry_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                        return false;
                    }
                    break;
                    
                 case SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY:
                    status = sai_tunnel_api->get_tunnel_term_table_entry_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                        return false;
                    }
                    break;
                 case SAI_OBJECT_TYPE_TUNNEL:
                    status = sai_tunnel_api->get_tunnel_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                        return false;
                    }
                    break;
                     
                 case SAI_OBJECT_TYPE_TUNNEL_MAP:
                    status = sai_tunnel_api->get_tunnel_map_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                    return false;
                        }
                    break;
                    
                 case SAI_OBJECT_TYPE_ROUTER_INTERFACE:
                    status = sai_router_intfs_api->get_router_interface_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                     if (status != SAI_STATUS_SUCCESS) {
                         return false;
                     }
                     break;
                     
                 case SAI_OBJECT_TYPE_NEXT_HOP:    
                    status = sai_next_hop_api->get_next_hop_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                     if (status != SAI_STATUS_SUCCESS) {
                         return false;
                     }
                     break;
                     
                 case SAI_OBJECT_TYPE_ROUTE_ENTRY:
                    status = sai_route_api->get_route_entry_attribute(route_entry, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                        return false;
                    }
                    break;
                    
                  case SAI_OBJECT_TYPE_VIRTUAL_ROUTER:
                    status = sai_virtual_router_api->get_virtual_router_attribute(object_id, (uint32_t)act_attr.size(),  act_attr.data());
                    if (status != SAI_STATUS_SUCCESS) {
                         return false;
                    }
                    break; 
                    
                default:
                    return false;
            }
            auto b_attr_eq = AttrListEq_for_two_sai_attr(objecttype, act_attr, exp_attrlist);
            if (!b_attr_eq) {
                return false;
            }
    
            return true;
        }


        bool create_vxlan_tunnel(string tunnel_name, string src_ip)
        {
            auto vxlan_tunnel_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VXLAN_TUNNEL_TABLE_NAME, 1, 1), vxlan_tunnel_orch, APP_VXLAN_TUNNEL_TABLE_NAME));
            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_name,
                        SET_COMMAND,
                        {
                          {"src_ip", src_ip},
                        }
                    } });
            Portal::ConsumerInternal::addToSync(vxlan_tunnel_consumer.get(), setData);
            ((Orch *) vxlan_tunnel_orch)->doTask(*vxlan_tunnel_consumer.get());

            if(vxlan_tunnel_orch->vxlan_tunnel_table_.find(tunnel_name)== vxlan_tunnel_orch->vxlan_tunnel_table_.end())
                return false;

            return true;

        }

        bool check_vxlan_tunnel(string tunnel_name, string src_ip)
        {
            const auto tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(tunnel_name);
            const auto decap_id = tunnel_obj->getDecapMapId();
            const auto encap_id = tunnel_obj->getEncapMapId();

            //check that the vxlan tunnel termination are there
            {   
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                
                attr.id = SAI_TUNNEL_MAP_ATTR_TYPE;
                attr.value.s32 = SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID;
                attrs.push_back(attr);
            
                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL_MAP, attrs, decap_id) == false)
                    return false;
            }

            {   
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                
                attr.id = SAI_TUNNEL_MAP_ATTR_TYPE;
                attr.value.s32 = SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI;
                attrs.push_back(attr);
       
                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL_MAP, attrs, encap_id) == false)
                    return false;
            }
            
            {   
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                
                attr.id = SAI_TUNNEL_ATTR_TYPE;
                attr.value.s32 = SAI_TUNNEL_TYPE_VXLAN;
                attrs.push_back(attr);
            
                attr.id = SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE;
                attr.value.oid = gUnderlayIfId;
                attrs.push_back(attr);

                sai_object_id_t decap_list[] = { decap_id };
                attr.id = SAI_TUNNEL_ATTR_DECAP_MAPPERS;
                attr.value.objlist.count = 1;
                attr.value.objlist.list = decap_list;
                attrs.push_back(attr);

                sai_object_id_t encap_list[] = { encap_id };
                attr.id = SAI_TUNNEL_ATTR_ENCAP_MAPPERS;
                attr.value.objlist.count = 1;
                attr.value.objlist.list = encap_list;
                attrs.push_back(attr);
                
                sai_ip_address_t ip;
                copy(ip, IpAddress(src_ip));        
                attr.id = SAI_TUNNEL_ATTR_ENCAP_SRC_IP;
                attr.value.ipaddr = ip;
                attrs.push_back(attr);
       
                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL, attrs, tunnel_obj->ids_.tunnel_id) == false)
                    return false;
            }

            {   
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                
                attr.id = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE;
                attr.value.s32 = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE;
                attrs.push_back(attr);

                attr.id = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID;
                attr.value.oid = gVirtualRouterId;
                attrs.push_back(attr);


                sai_ip_address_t ip;
                copy(ip, IpAddress(src_ip));
                attr.id = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP;
                attr.value.ipaddr = ip;
                attrs.push_back(attr);

                attr.id = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE;
                attr.value.s32 = SAI_TUNNEL_TYPE_VXLAN;
                attrs.push_back(attr);

                attr.id = SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID;
                attr.value.oid = tunnel_obj->ids_.tunnel_id;
                attrs.push_back(attr);
                

                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY, attrs, tunnel_obj->ids_.tunnel_term_id) == false)
                    return false;
            }

        }

        
        bool check_vxlan_tunnel_entry(string tunnel_name, string vnet_name, uint32_t vni_id)
        {
            const auto tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(tunnel_name);
            const auto decap_id = tunnel_obj->getDecapMapId();
            const auto encap_id = tunnel_obj->getEncapMapId();

            const auto encap_tunnel_map_id = tunnel_obj->tunnel_map_entries_[vni_id].first;
            const auto decap_tunnel_map_id = tunnel_obj->tunnel_map_entries_[vni_id].second; 
            auto *vnet_obj = vnet_orch->getTypePtr<VNetVrfObject>(vnet_name);


            {
                sai_attribute_t attr;
                std::vector<sai_attribute_t> tunnel_map_entry_attrs;  
                
                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE;
                attr.value.s32 = SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI;//e
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP;
                attr.value.oid = encap_id;
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_KEY;
                attr.value.oid = vnet_obj->getVRidIngress();
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE;
                attr.value.u32 = vni_id;
                tunnel_map_entry_attrs.push_back(attr);
                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel_map_entry_attrs, encap_tunnel_map_id) == false)
                    return false;
            }

            {
                sai_attribute_t attr;
                std::vector<sai_attribute_t> tunnel_map_entry_attrs;
                
                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE;
                attr.value.s32 = SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID;
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP;
                attr.value.oid = decap_id;
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_VALUE;
                attr.value.oid = vnet_obj->getVRidEgress();
                tunnel_map_entry_attrs.push_back(attr);

                attr.id = SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY;
                attr.value.u32 = vni_id;
                tunnel_map_entry_attrs.push_back(attr);

                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel_map_entry_attrs, decap_tunnel_map_id) == false)
                    return false;
            }
            return true;
        }
        
        bool create_vnet_entry(string name, string tunnel, string vni, string peer_list)
        {
            auto vnet_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_configDb.get(), APP_VNET_TABLE_NAME, 1, 1), vnet_orch, APP_VNET_TABLE_NAME));

            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { name,
                        SET_COMMAND,
                        {
                            {"vxlan_tunnel", tunnel},
                            {"vni", vni},
                            {"peer_list", peer_list},

                        }
                    } });       

            Portal::ConsumerInternal::addToSync(vnet_consumer.get(),setData);
            vnet_orch->doTask(*vnet_consumer.get());
            if(vnet_orch->vnet_table_.find(name) == vnet_orch->vnet_table_.end())
                return false;            
            return true;

        }

        bool check_vnet_entry(string name)
        {
            //check asic db size
            
          /* if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, attrs, port.m_rif_id ) == false)
             return false;
          */
        }
        
        bool create_vlan_interface(int vlan_id, string ifname, string vnet_name, string ipaddr)
        {
            //create vlan
            string vlan_name = "Vlan" + to_string(vlan_id);
            auto vlan_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VLAN_TABLE_NAME, 1, 1), gPortsOrch, APP_VLAN_TABLE_NAME));
            auto vlan_setData = deque<KeyOpFieldsValuesTuple>(
                    { { vlan_name,
                        SET_COMMAND,
                        {
                            {"vlanid", to_string(vlan_id)},
                            {"mtu", "9100"}              
                        }
                    } });  
            Portal::ConsumerInternal::addToSync(vlan_consumer.get(),vlan_setData);
            gPortsOrch->doTask(*vlan_consumer.get());

            auto vlanmember_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VLAN_MEMBER_TABLE_NAME, 1, 1), gPortsOrch, APP_VLAN_MEMBER_TABLE_NAME)); 
            string name = vlan_name + "|" + ifname;
            auto vlanmember_setData = deque<KeyOpFieldsValuesTuple>(
                    { { name,
                        SET_COMMAND,
                        {
                            {"tagging_mode",  "untagged"},
                        }
                    } });       
            
            Portal::ConsumerInternal::addToSync(vlanmember_consumer.get(),vlanmember_setData);
            gPortsOrch->doTask(*vlanmember_consumer.get());
        
            auto intf_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_INTF_TABLE_NAME, 1, 1), gIntfsOrch, APP_INTF_TABLE_NAME));
            auto intf_setData = deque<KeyOpFieldsValuesTuple>(
                    { { vlan_name,
                        SET_COMMAND,
                        {
                            {"vnet_name",  vnet_name},
                        }
                    } });  

            Portal::ConsumerInternal::addToSync(intf_consumer.get(),intf_setData);
            gIntfsOrch->doTask(*intf_consumer.get());   

            string intf_name = vlan_name + "|" + ipaddr;
            auto vlan_intf_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VLAN_MEMBER_TABLE_NAME, 1, 1), gPortsOrch, APP_VLAN_MEMBER_TABLE_NAME));
            auto vlan_intf_setData = deque<KeyOpFieldsValuesTuple>(
                    { { intf_name,
                        SET_COMMAND,
                        {
                            {"family",  "IPv4"},
                        }
                    } });
            Portal::ConsumerInternal::addToSync(vlan_intf_consumer.get(),vlan_intf_setData);
            gPortsOrch->doTask(*vlan_intf_consumer.get());    
            return true;
        }


        bool create_phy_interface(string ifname, string vnet_name, string ipaddr)
        {
            string name = ifname + "|" + ipaddr;
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_PORT_TABLE_NAME, 1, 1), gPortsOrch, APP_PORT_TABLE_NAME));
            
            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { ifname,
                        SET_COMMAND,
                        {
                            {"vnet_name",  vnet_name},
                            {"mtu", "9100"}     
                        }
                    } });
            Portal::ConsumerInternal::addToSync(consumer.get(),setData);
            gPortsOrch->doTask(*consumer.get()); 

            auto intf_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_INTF_TABLE_NAME, 1, 1), gIntfsOrch, APP_INTF_TABLE_NAME));
            auto intf_setData = deque<KeyOpFieldsValuesTuple>(
                    { { ifname,
                        SET_COMMAND,
                        {
                            {"vnet_name",  vnet_name},
                        }
                    } });  
            
            Portal::ConsumerInternal::addToSync(intf_consumer.get(),intf_setData);
            gIntfsOrch->doTask(*intf_consumer.get());   
            
            auto setData_family = deque<KeyOpFieldsValuesTuple>(
                    { { name,
                        SET_COMMAND,
                        {
                            {"family",  "IPv4"},
                        }
                    } });
            Portal::ConsumerInternal::addToSync(consumer.get(),setData_family);
            gPortsOrch->doTask(*consumer.get()); 
            
            return true;
        }
        
        bool check_router_interface(string name, int vlan=0, string if_name=""){
            //Check RIF in ingress VRF

            auto *vnet_obj = vnet_orch->getTypePtr<VNetVrfObject>(name);
            string vlan_name;
            sai_object_id_t vlan_oid;
            Port port;

            if(vlan){
                vlan_name = "Vlan" + to_string(vlan);
                gPortsOrch->getPort(vlan_name, port);
                vlan_oid = port.m_vlan_info.vlan_oid;
            }else{
                gPortsOrch->getPort(if_name, port);
            }

            sai_attribute_t attr;
            std::vector<sai_attribute_t> attrs;
            
            attr.id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
            attr.value.oid = vnet_obj->getVRidIngress();
            attrs.push_back(attr);
            
            attr.id = SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS;
            memcpy(attr.value.mac, gMacAddress.getMac(), sizeof(sai_mac_t));
            attrs.push_back(attr);
            
            attr.id = SAI_ROUTER_INTERFACE_ATTR_MTU;
            attr.value.u32 = 9100;
            attrs.push_back(attr);

            if(vlan)
            {
                attr.id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
                attr.value.s32 = SAI_ROUTER_INTERFACE_TYPE_VLAN;
                attrs.push_back(attr);
                
                attr.id = SAI_ROUTER_INTERFACE_ATTR_VLAN_ID;
                attr.value.oid = vlan_oid;
                attrs.push_back(attr);                
            }
            else
            {
                attr.id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
                attr.value.s32 = SAI_ROUTER_INTERFACE_TYPE_PORT;
                attrs.push_back(attr);            
            }
            
            if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_ROUTER_INTERFACE, attrs, port.m_rif_id) == false){
                return false;
            }
            return true;

        }
        
        bool create_vnet_routes(string prefix, string vnet_name, string endpoint, string mac="",int vni=0)
        {
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VNET_RT_TUNNEL_TABLE_NAME, 1, 1), vnet_rt_orch, APP_VNET_RT_TUNNEL_TABLE_NAME));
            string name = vnet_name + ":" + prefix;

            vector<FieldValueTuple> setData =
            { 
                {"endpoint",  endpoint},
            };

            if (vni)
                setData.push_back({"vni", to_string(vni)});
            
            if (mac!="")
                setData.push_back({"mac_address", mac});
            
            auto setDatas = deque<KeyOpFieldsValuesTuple>(
                    { { name,
                        SET_COMMAND,
                        {
                          setData,
                        }
                    } }); 
            Portal::ConsumerInternal::addToSync(consumer.get(),setDatas);
            vnet_rt_orch->doTask(*consumer.get());  

            return true;
        }

        bool check_vnet_routes(string name,string endpoint, string tunnel, string prefix, string mac="", int vni=0)
        {
            auto tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(tunnel);
            sai_object_id_t tunnel_id = tunnel_obj->getTunnelId();   
            IpAddress endpoint_ip = IpAddress(endpoint);
            MacAddress tunnel_mac = mac!="" ? MacAddress(mac):gVxlanMacAddress;
            sai_object_id_t nh_id = tunnel_obj->getNextHop(endpoint_ip, tunnel_mac, vni);
            auto *vnet_obj = vnet_orch->getTypePtr<VNetVrfObject>(name);

            // Check routes in ingress VRF
            {
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                
                attr.id = SAI_NEXT_HOP_ATTR_TYPE;
                attr.value.s32 = SAI_NEXT_HOP_TYPE_IP;
                attrs.push_back(attr);

                sai_ip_address_t ip;
                copy(ip, IpAddress(endpoint));
                attr.id = SAI_NEXT_HOP_ATTR_IP;
                attr.value.ipaddr = ip;
                attrs.push_back(attr);

                attr.id = SAI_NEXT_HOP_ATTR_TUNNEL_ID;
                attr.value.oid = tunnel_id;
                attrs.push_back(attr);
                

                if (vni)
                {
                    attr.id = SAI_NEXT_HOP_ATTR_TUNNEL_VNI;
                    attr.value.u32 = vni;
                    attrs.push_back(attr);
                }

                if (mac!="")
                {     
                    attr.id = SAI_NEXT_HOP_ATTR_TUNNEL_MAC;
                    memcpy(attr.value.mac, tunnel_mac.getMac(), sizeof(sai_mac_t));
                    attrs.push_back(attr);
                }

                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_NEXT_HOP, attrs, nh_id) == false)
                    return false;
            }
            
            //Check if the route is in expected VRF          
            {
                sai_attribute_t attr;
                std::vector<sai_attribute_t> attrs;
                sai_route_entry_t route_entry;
                route_entry.vr_id = vnet_obj->getVRidIngress();
                route_entry.switch_id = gSwitchId;
                IpPrefix ip_prefix = IpPrefix(prefix);
                copy(route_entry.destination,ip_prefix);

                attr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
                attr.value.oid = nh_id;
                attrs.push_back(attr);
                
                if (validate_vxlan_tunnel(SAI_OBJECT_TYPE_ROUTE_ENTRY, attrs, 0, &route_entry ) == false)
                    return false;
            }
            return true;
        }
        
        bool create_vnet_local_routes(string prefix, string vnet_name, string ifname)
        {
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VNET_RT_TABLE_NAME, 1, 1), vnet_rt_orch, APP_VNET_RT_TABLE_NAME));
            string name = vnet_name + ":" + prefix; 

            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { name,
                        SET_COMMAND,
                        {
                            {"ifname",  ifname},
                        }
                    } });       

            Portal::ConsumerInternal::addToSync(consumer.get(),setData);
            vnet_rt_orch->doTask(*consumer.get());  
            return true;         
        }

    };

    //Test 1 - Create Vlan Interface, Tunnel and Vnet
    TEST_F(VnetOrchTest, test_vnet_orch_1)
    {
        string tunnel_name = "tunnel_1";

        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "10.10.10.10") == true);
        ASSERT_TRUE(create_vnet_entry("Vnet_2000", tunnel_name, "2000", "")== true);

        //check_vnet_entry("Vnet_2000");
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_2000", 2000)== true);
        ASSERT_TRUE(check_vxlan_tunnel(tunnel_name, "10.10.10.10")== true);
        
        ASSERT_TRUE(create_vlan_interface(100, "Ethernet24", "Vnet_2000", "100.100.3.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_2000", 100)== true);
        
        ASSERT_TRUE(create_vlan_interface(101, "Ethernet28", "Vnet_2000", "100.100.4.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_2000", 101)== true);
        
        ASSERT_TRUE(create_vnet_routes("100.100.1.1/32", "Vnet_2000", "10.10.10.1")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_2000", "10.10.10.1", tunnel_name,"100.100.1.1/32")== true);

        
        ASSERT_TRUE(create_vnet_local_routes("100.100.3.0/24", "Vnet_2000", "Vlan100")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_2000")== true); //check asic size
        ASSERT_TRUE(create_vnet_local_routes("100.100.4.0/24", "Vnet_2000", "Vlan101")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_2000")== true); //check asic size

        //Create Physical Interface in another Vnet
        
        ASSERT_TRUE(create_vnet_entry("Vnet_2001", tunnel_name, "2001", "")== true);
        //check_vnet_entry(dvs, 'Vnet_2000')
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_2001", 2001)== true);

        
        ASSERT_TRUE(create_phy_interface("Ethernet4", "Vnet_2001", "100.102.1.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_2001",0,"Ethernet4")== true);
        
        ASSERT_TRUE(create_vnet_routes("100.100.2.1/32", "Vnet_2001", "10.10.10.2", "00:12:34:56:78:9A")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_2001", "10.10.10.2", tunnel_name,"100.100.2.1/32","00:12:34:56:78:9A")== true);

                
        ASSERT_TRUE(create_vnet_local_routes("100.102.1.0/24", "Vnet_2001", "Ethernet4")== true);  
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_2001")== true);

    }

#if 1    
    //Test 2 - Two VNets, One HSMs per VNet

    TEST_F(VnetOrchTest,test_vnet_orch_2)
    {
        string tunnel_name = "tunnel_2";
        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "6.6.6.6") == true);
        ASSERT_TRUE(create_vnet_entry("Vnet_1", tunnel_name, "1111", "")== true);

        //check_vnet_entry("Vnet_1"); 
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_1", 1111)== true);
        ASSERT_TRUE(check_vxlan_tunnel(tunnel_name, "6.6.6.6")== true);
        
        ASSERT_TRUE(create_vlan_interface(1001, "Ethernet0", "Vnet_1", "1.1.10.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_1", 1001)== true);

        
        ASSERT_TRUE(create_vnet_routes("1.1.1.10/32", "Vnet_1", "100.1.1.10")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_1", "100.1.1.10", tunnel_name,"1.1.1.10/32")== true);
        
        ASSERT_TRUE(create_vnet_routes("1.1.1.11/32", "Vnet_1", "100.1.1.10")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_1", "100.1.1.10", tunnel_name,"1.1.1.11/32")== true);        
    
        ASSERT_TRUE(create_vnet_routes("1.1.1.12/32", "Vnet_1", "200.200.1.200")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_1", "200.200.1.200", tunnel_name,"1.1.1.12/32")== true);
      
        ASSERT_TRUE(create_vnet_routes("1.1.1.14/32", "Vnet_1", "200.200.1.201")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_1", "200.200.1.201", tunnel_name,"1.1.1.14/32")== true);  
        
        ASSERT_TRUE(create_vnet_local_routes("1.1.10.0/24", "Vnet_1", "Vlan1001")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_1")== true); //check asic size
        
        ASSERT_TRUE(create_vnet_entry("Vnet_2", tunnel_name, "2222", "")== true);
        //check_vnet_entry("Vnet_2"); 
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_2", 2222)== true);

        ASSERT_TRUE(create_vlan_interface(1002, "Ethernet4", "Vnet_2", "2.2.10.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_2", 1002)== true);
                
        ASSERT_TRUE(create_vnet_routes("2.2.2.10/32", "Vnet_2", "100.1.1.20")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_2", "100.1.1.20", tunnel_name,"2.2.2.10/32")== true);  
        
        ASSERT_TRUE(create_vnet_routes("2.2.2.11/32", "Vnet_2", "100.1.1.20")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_2", "100.1.1.20", tunnel_name,"2.2.2.11/32")== true); 
                
        ASSERT_TRUE(create_vnet_local_routes( "2.2.10.0/24", "Vnet_2", "Vlan1002")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_2")== true); //check asic size
    }

    //Test 3 - Two VNets, One HSMs per VNet, Peering
    TEST_F(VnetOrchTest,test_vnet_orch_3)
    {

        string tunnel_name = "tunnel_3";

        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "7.7.7.7")== true);

        ASSERT_TRUE(create_vnet_entry("Vnet_10", tunnel_name, "3333", "Vnet_20")== true);
        //check_vnet_entry("Vnet_10",{"Vnet_20"}); 
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_10", 3333)== true);
        
        ASSERT_TRUE(create_vnet_entry("Vnet_20", tunnel_name, "4444", "Vnet_10")== true);
        //check_vnet_entry("Vnet_20",{"Vnet_10"}); 
        ASSERT_TRUE(check_vxlan_tunnel_entry(tunnel_name, "Vnet_20", 4444)== true);
        ASSERT_TRUE(check_vxlan_tunnel(tunnel_name, "7.7.7.7")== true);
           
        ASSERT_TRUE(create_vlan_interface(2001, "Ethernet8", "Vnet_10", "5.5.10.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_10", 2001)== true);
        
        ASSERT_TRUE(create_vlan_interface(2002, "Ethernet12", "Vnet_20", "8.8.10.1/24")== true);
        ASSERT_TRUE(check_router_interface("Vnet_20", 2002)== true);
        

        ASSERT_TRUE(create_vnet_routes("5.5.5.10/32", "Vnet_10", "50.1.1.10")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_10", "50.1.1.10", tunnel_name,"5.5.5.10/32")== true); 

        ASSERT_TRUE(create_vnet_routes( "8.8.8.10/32", "Vnet_20", "80.1.1.20")== true);
        ASSERT_TRUE(check_vnet_routes("Vnet_10", "80.1.1.20", tunnel_name,"8.8.8.10/32")== true); 

        ASSERT_TRUE(create_vnet_local_routes("5.5.10.0/24", "Vnet_10", "Vlan2001")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_10")== true); //check asic size

        ASSERT_TRUE(create_vnet_local_routes("8.8.10.0/24", "Vnet_20", "Vlan2002")== true);
        // ASSERT_TRUE(check_vnet_local_routes("Vnet_20")== true); //check asic size
        
    }
 #endif
 
}

