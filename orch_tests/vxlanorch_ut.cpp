#include <random>
#include "ut_helper.h"
#define protected public
#define private public
#include "orch.h"
#include "orchdaemon.h"
#include "vxlanorch.h"
#undef protected
#undef private
#include "saiattributelist.h"
#include "saihelper.h"
#include "swssnet.h"
#include "tokenize.h"
#include "subscriberstatetable.h"

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
extern Directory<Orch*> gDirectory;

const map<MAP_T, uint32_t> vxlanTunnelMap =
{
    { MAP_T::VNI_TO_VLAN_ID, SAI_TUNNEL_MAP_TYPE_VNI_TO_VLAN_ID },
    { MAP_T::VLAN_ID_TO_VNI, SAI_TUNNEL_MAP_TYPE_VLAN_ID_TO_VNI },
    { MAP_T::VRID_TO_VNI, SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI },
    { MAP_T::VNI_TO_VRID, SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID },
    { MAP_T::BRIDGE_TO_VNI, SAI_TUNNEL_MAP_TYPE_BRIDGE_IF_TO_VNI },
    { MAP_T::VNI_TO_BRIDGE,  SAI_TUNNEL_MAP_TYPE_VNI_TO_BRIDGE_IF},
};

const map<MAP_T, std::pair<uint32_t, uint32_t>> vxlanTunnelMapKeyVal =
{
    { MAP_T::VNI_TO_VLAN_ID,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_VLAN_ID_VALUE }
    },
    { MAP_T::VLAN_ID_TO_VNI,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_VLAN_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE }
    },
    { MAP_T::VRID_TO_VNI,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE }
    },
    { MAP_T::VNI_TO_VRID,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_VALUE }
    },
    { MAP_T::BRIDGE_TO_VNI,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_BRIDGE_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE }
    },
    { MAP_T::VNI_TO_BRIDGE,
        { SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY, SAI_TUNNEL_MAP_ENTRY_ATTR_BRIDGE_ID_VALUE }
    },
};

/*
 * Manipulators for the above Map
 */
static inline uint32_t tunnel_map_type (MAP_T map_t)
{
    return vxlanTunnelMap.at(map_t);
}

static inline uint32_t tunnel_map_key (MAP_T map_t)
{
    return vxlanTunnelMapKeyVal.at(map_t).first;
}

static inline uint32_t tunnel_map_val (MAP_T map_t)
{
    return vxlanTunnelMapKeyVal.at(map_t).second;
}

static const char* profile_get_value(
    _In_ sai_switch_profile_id_t profile_id,
    _In_ const char* variable)
{
    // UNREFERENCED_PARAMETER(profile_id);

    if (!strcmp(variable, "SAI_KEY_INIT_CONFIG_FILE")) {
        return "/usr/share/sai_2410.xml"; // FIXME: create a json file, and passing the path into test
    } else if (!strcmp(variable, "KV_DEVICE_MAC_ADDRESS")) {
        return "20:03:04:05:06:00";
    } else if (!strcmp(variable, "SAI_KEY_L3_ROUTE_TABLE_SIZE")) {
        return "1000";
    } else if (!strcmp(variable, "SAI_KEY_L3_NEIGHBOR_TABLE_SIZE")) {
        return "2000";
    } else if (!strcmp(variable, "SAI_VS_SWITCH_TYPE")) {
        return "SAI_VS_SWITCH_TYPE_BCM56850";
    }

    return NULL;
}

static int profile_get_next_value(
    _In_ sai_switch_profile_id_t profile_id,
    _Out_ const char** variable,
    _Out_ const char** value)
{
    if (value == NULL) {
        return 0;
    }

    if (variable == NULL) {
        return -1;
    }

    return -1;
}

namespace VxlanOrchCppTest
{
    using namespace testing;

    shared_ptr<DBConnector> m_configDb;
    shared_ptr<DBConnector> m_applDb;
    shared_ptr<DBConnector> m_stateDb;

    static map<string, string> gProfileMap;
    static map<string, string>::iterator gProfileIter;

    static const char* profile_get_value(
        sai_switch_profile_id_t profile_id,
        const char* variable)
    {
        map<string, string>::const_iterator it = gProfileMap.find(variable);
        if (it == gProfileMap.end()) {
            return NULL;
        }

        return it->second.c_str();
    }

    static int profile_get_next_value(
        sai_switch_profile_id_t profile_id,
        const char** variable,
        const char** value)
    {
        if (value == NULL) {
            gProfileIter = gProfileMap.begin();
            return 0;
        }

        if (variable == NULL) {
            return -1;
        }

        if (gProfileIter == gProfileMap.end()) {
            return -1;
        }

        *variable = gProfileIter->first.c_str();
        *value = gProfileIter->second.c_str();

        gProfileIter++;

        return 0;
    }

size_t consumerAddToSync(Consumer* consumer, const deque<KeyOpFieldsValuesTuple>& entries)
{
    /* Nothing popped */
    if (entries.empty())
    {
        return 0;
    }

    for (auto& entry : entries)
    {
        string key = kfvKey(entry);
        string op = kfvOp(entry);

        /* If a new task comes or if a DEL task comes, we directly put it into getConsumerTable().m_toSync map */
        if (consumer->m_toSync.find(key) == consumer->m_toSync.end() || op == DEL_COMMAND)
        {
            consumer->m_toSync[key] = entry;
        }
        /* If an old task is still there, we combine the old task with new task */
        else
        {
            KeyOpFieldsValuesTuple existing_data = consumer->m_toSync[key];

            auto new_values = kfvFieldsValues(entry);
            auto existing_values = kfvFieldsValues(existing_data);

            for (auto it : new_values)
            {
                string field = fvField(it);
                string value = fvValue(it);

                auto iu = existing_values.begin();
                while (iu != existing_values.end())
                {
                    string ofield = fvField(*iu);
                    if (field == ofield)
                        iu = existing_values.erase(iu);
                    else
                        iu++;
                }
                existing_values.push_back(FieldValueTuple(field, value));
            }
            consumer->m_toSync[key] = KeyOpFieldsValuesTuple(key, op, existing_values);
        }
    }
    return entries.size();
}


    class OrchagentStub
    {
    public:
        sai_status_t saiInit()
        {
            // Init switch and create dependencies

            gProfileMap.emplace("SAI_VS_SWITCH_TYPE", "SAI_VS_SWITCH_TYPE_BCM56850");
            gProfileMap.emplace("KV_DEVICE_MAC_ADDRESS", "20:03:04:05:06:00");

            static sai_service_method_table_t test_services = {
                profile_get_value,
                profile_get_next_value
            };

            auto status = sai_api_initialize(0, (sai_service_method_table_t*)&test_services);
            if(status != SAI_STATUS_SUCCESS) {
                return status;
            }

            sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);
            sai_api_query(SAI_API_TUNNEL, (void**)&sai_tunnel_api);
            sai_api_query(SAI_API_NEXT_HOP, (void**)&sai_next_hop_api);
            sai_api_query(SAI_API_ROUTER_INTERFACE, (void**)&sai_router_intfs_api);
            sai_api_query(SAI_API_PORT, (void**)&sai_port_api);
            sai_api_query(SAI_API_VLAN, (void**)&sai_vlan_api);
            sai_api_query(SAI_API_BRIDGE, (void**)&sai_bridge_api);
            sai_api_query(SAI_API_VIRTUAL_ROUTER, (void**)&sai_virtual_router_api);

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

            sai_api_uninitialize();

            sai_switch_api = nullptr;
            sai_qos_map_api = nullptr;
            sai_tunnel_api = nullptr;
            sai_next_hop_api = nullptr;
            sai_port_api = nullptr;
            sai_vlan_api = nullptr;
            sai_bridge_api = nullptr;
            return status;
        }

        sai_status_t createVirtualRouter(sai_object_id_t& obj_id)
        {
            random_device rd;
            default_random_engine gen = default_random_engine(rd());
            uniform_int_distribution<int> dis(1,9);
            sai_attribute_t attr;

            vector<sai_attribute_t> attrs;
            auto mac = MacAddress("02:04:06:08:0a:0" + to_string(dis(gen)));
            attr.id = SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS;
            memcpy(attr.value.mac, mac.getMac(), ETHER_ADDR_LEN);
            attrs.push_back(attr);

            return sai_virtual_router_api->create_virtual_router(&obj_id,
                                                                 gSwitchId,
                                                                 static_cast<uint32_t>(attrs.size()),
                                                                 attrs.data());

        }

        void createVlanInPortorch(uint16_t vlan_id)
        {
            //ConsumerExtend *consumer_port;
            string vlan_alias = "Vlan" + to_string(vlan_id);
            port_table_init();
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VLAN_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gPortsOrch, APP_VLAN_TABLE_NAME));
            auto setData = deque<KeyOpFieldsValuesTuple>(
                { { vlan_alias,
                    SET_COMMAND,
                    {
                      {"vlanid", to_string(vlan_id)},
                      {"mtu", "9100"}
                    }
                } });
            consumerAddToSync(consumer.get(), setData);
            ((Orch *) gPortsOrch)->doTask(*consumer.get());
            //consumer_port->addToSync(setData_port);
            //((Orch *) gPortsOrch)->doTask(*consumer_port);
        }

        void createVrfInVrforch(string vrf_name)
        {
            random_device rd;
            default_random_engine gen = default_random_engine(rd());
            uniform_int_distribution<int> dis(1,9);
            auto mac_string = "02:04:06:08:0a:0" + to_string(dis(gen));
            auto vrf_orch = gDirectory.get<VRFOrch*>();
            if(!vrf_orch) {
                vrf_orch = new VRFOrch(m_applDb.get(), APP_VRF_TABLE_NAME);
                gDirectory.set(vrf_orch);
            }
            //ConsumerExtend *consumer_vrf = new ConsumerExtend(new ConsumerStateTable(m_applDb.get(), string(APP_VRF_TABLE_NAME), 1, 1), vrf_orch, APP_VRF_TABLE_NAME);
            //auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VRF_TABLE_NAME, 1, 1), vrf_orch, APP_VRF_TABLE_NAME));
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VRF_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vrf_orch, APP_VRF_TABLE_NAME));   
            auto setData = deque<KeyOpFieldsValuesTuple>(
                { { vrf_name,
                    SET_COMMAND,
                    {
                      {"v4", "true"},
                      {"v6", "false"},
                      {"src_mac", mac_string},
                      {"ttl_action", "copy"},
                      {"ip_opt_action", "deny"},
                      {"l3_mc_action", "drop"}
                    }
                } });
            consumerAddToSync(consumer.get(), setData);
            ((Orch *) vrf_orch)->doTask(*consumer.get());
            //consumer_vrf->addToSync(setData_vrf);
            //((Orch *) vrf_orch)->doTask(*consumer_vrf);
        }

        void createTunnelInTunnelorch(string tunnel_name)
        {
            auto vxlan_tunnel_orch = gDirectory.get<VxlanTunnelOrch*>();
            if (!vxlan_tunnel_orch)
            {
                vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
            }
            
            //ConsumerExtend *vxlan_tunnel_consumer = new ConsumerExtend(new ConsumerStateTable(m_configDb.get(), string(CFG_VXLAN_TUNNEL_TABLE_NAME), 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME);
            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_name,
                        SET_COMMAND,
                        {
                          {"src_ip", "91.91.91.92"},
                          {"dst_ip", "101.101.101.102"}
                        }
                    } });
            //auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME));
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME)); 
            
            consumerAddToSync(consumer.get(), setData);
            
            //vxlan_tunnel_consumer->addToSync(setData);
            ((Orch *) vxlan_tunnel_orch)->doTask(*consumer.get());
            VxlanTunnel tunnel1 = VxlanTunnel(tunnel_name, IpAddress("91.91.91.92"), IpAddress("101.101.101.102"));
            ASSERT_TRUE(tunnel1.createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID));
        }

    private:
        void port_table_init(void)
        {
            //auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_PORT_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gPortsOrch, APP_PORT_TABLE_NAME));
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_PORT_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gPortsOrch, APP_PORT_TABLE_NAME));
            
            consumerAddToSync(consumer.get(), { { "PortInitDone", EMPTY_PREFIX, {} } });
            ((Orch *) gPortsOrch)->doTask(*consumer.get());
            ASSERT_TRUE(gPortsOrch->isInitDone());
        }

    };

    OrchagentStub orchgent_stub;

    class VxlanTunnelTest : public Test
    {
    public:

        VxlanTunnelTest()
        {
        }

        ~VxlanTunnelTest()
        {
        }

        void SetUp()
        {
            ASSERT_TRUE(orchgent_stub.saiInit() == SAI_STATUS_SUCCESS);
        }

        void TearDown()
        {
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
        }

        void validateTunnelInSai(const VxlanTunnel &tunnel)
        {
            sai_attribute_t attr;
            auto encap_obj_id = tunnel.getEncapMapId();
            if (tunnel.tunnel_map_.first != MAP_T::MAP_TO_INVALID) {
                ASSERT_TRUE(encap_obj_id != SAI_NULL_OBJECT_ID);
                attr.id = SAI_TUNNEL_MAP_ATTR_TYPE;
                auto status = sai_tunnel_api->get_tunnel_map_attribute(encap_obj_id, 1, &attr);
                ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
                ASSERT_TRUE(attr.value.s32 == tunnel_map_type(tunnel.tunnel_map_.first));
            }
            else {
                ASSERT_TRUE(encap_obj_id == SAI_NULL_OBJECT_ID);
            }


            auto decap_obj_id = tunnel.getDecapMapId();
            if (tunnel.tunnel_map_.second != MAP_T::MAP_TO_INVALID)
            {
                ASSERT_TRUE(decap_obj_id != SAI_NULL_OBJECT_ID);
                attr.id = SAI_TUNNEL_MAP_ATTR_TYPE;
                auto status = sai_tunnel_api->get_tunnel_map_attribute(decap_obj_id, 1, &attr);
                ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
                ASSERT_TRUE(attr.value.s32 == tunnel_map_type(tunnel.tunnel_map_.second));
            }
            else
            {
                ASSERT_TRUE(decap_obj_id == SAI_NULL_OBJECT_ID);
            }

            auto tunnel_obj_id = tunnel.getTunnelId();
            ASSERT_TRUE(tunnel_obj_id != SAI_NULL_OBJECT_ID);
            attr.id = SAI_TUNNEL_ATTR_TYPE;
            auto status = sai_tunnel_api->get_tunnel_attribute(tunnel_obj_id, 1, &attr);
            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
            ASSERT_TRUE(attr.value.s32 == SAI_TUNNEL_TYPE_VXLAN);

            attr.id = SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE;
            status = sai_tunnel_api->get_tunnel_attribute(tunnel_obj_id, 1, &attr);
            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
            ASSERT_TRUE(attr.value.oid == gUnderlayIfId);

            attr.id = SAI_TUNNEL_ATTR_DECAP_MAPPERS;
            sai_object_id_t decap_list[1];
            attr.value.objlist.count = 1;
            attr.value.objlist.list = decap_list;
            status = sai_tunnel_api->get_tunnel_attribute(tunnel_obj_id, 1, &attr);

            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
            ASSERT_TRUE(decap_obj_id == decap_list[0]);

            if (encap_obj_id != SAI_NULL_OBJECT_ID)
            {
                sai_object_id_t encap_list[1];
                attr.id = SAI_TUNNEL_ATTR_ENCAP_MAPPERS;
                attr.value.objlist.count = 1;
                attr.value.objlist.list = encap_list;
                status = sai_tunnel_api->get_tunnel_attribute(tunnel_obj_id, 1, &attr);
                ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
                ASSERT_TRUE(encap_obj_id == encap_list[0]);
            }

            if (encap_obj_id != SAI_NULL_OBJECT_ID)
            {
                sai_ip_address_t src_ip;

                memset(&src_ip, 0, sizeof(sai_ip_address_t));
                copy(src_ip, tunnel.src_ip_);
                attr.id = SAI_TUNNEL_ATTR_ENCAP_SRC_IP;
                status = sai_tunnel_api->get_tunnel_attribute(tunnel_obj_id, 1, &attr);
                ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
                ASSERT_TRUE(memcmp(&attr.value.ipaddr, &src_ip, sizeof(sai_ip_address_t)) == 0);
            }
        }

    };

    TEST_F(VxlanTunnelTest, CreateTunnel) {
        const map<MAP_T, MAP_T> tunnel_map_pattern =
        {
            /*  enacap type             decap type       */
            { MAP_T::MAP_TO_INVALID, MAP_T::VNI_TO_VLAN_ID},
            { MAP_T::VRID_TO_VNI,    MAP_T::VNI_TO_VRID},
            { MAP_T::BRIDGE_TO_VNI,  MAP_T::VNI_TO_BRIDGE},
        };

        const map<IpAddress, IpAddress> tunnel_ip_pattern =
        {
            /*  tunnel scr ip          tunnel dst ip   */
            { IpAddress("0.0.0.0"), IpAddress("2.2.2.2")},
            { IpAddress("1.1.1.1"), IpAddress("0.0.0.0")},
            { IpAddress("1.1.1.1"), IpAddress("2.2.2.2")},
            { IpAddress("2.2.2.2"), IpAddress("1.1.1.1")},
        };

        for (auto const& x : tunnel_map_pattern)
        {
            for (auto const& ip_pattern : tunnel_ip_pattern)
            {
                VxlanTunnel tunnel = VxlanTunnel("tunnel", ip_pattern.first, ip_pattern.second);
                ASSERT_TRUE(tunnel.createTunnel(x.first, x.second));
                validateTunnelInSai(tunnel);
            }
        }
    }


    TEST_F(VxlanTunnelTest, NextHopRefCount)
    {
        string tunnel_name = "tunnel";
        IpAddress src_ip = IpAddress("1.1.1.1"), dst_ip = IpAddress("2.2.2.2");
        uint32_t vni = 10000;
        VxlanTunnelTable vxlan_tunnel_table_;
        MacAddress macAddress = MacAddress("52:54:00:25:12:E9");
        sai_object_id_t nh_id = 50;

        auto tunnel_obj = new VxlanTunnel(tunnel_name, src_ip, dst_ip);
        tunnel_obj->createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID);

        tunnel_obj->insertMapperEntry(SAI_NULL_OBJECT_ID, SAI_NULL_OBJECT_ID, vni);
        tunnel_obj->updateNextHop(src_ip, macAddress, vni, nh_id);
        tunnel_obj->getNextHop(src_ip, macAddress, vni);
        auto key = nh_key_t(src_ip, macAddress, vni);
        auto ref_count = tunnel_obj->nh_tunnels_[key].ref_count;
        tunnel_obj->incNextHopRefCount(src_ip, macAddress, vni);
        ASSERT_EQ((ref_count + 1), tunnel_obj->nh_tunnels_[key].ref_count);
        tunnel_obj->incNextHopRefCount(src_ip, macAddress, vni);
        ASSERT_EQ((ref_count + 2), tunnel_obj->nh_tunnels_[key].ref_count);
        tunnel_obj->decNextHopRefCount(src_ip, macAddress, vni);
        ASSERT_EQ((ref_count + 1), tunnel_obj->nh_tunnels_[key].ref_count);
        tunnel_obj->removeNextHop(src_ip, macAddress, vni);

        try{
            tunnel_obj->removeNextHop(src_ip, macAddress, vni);
        }
        catch(const runtime_error& error) {
            ASSERT_TRUE(true);
        }
    }

	class VxlanTunnelOrchTest : public Test
    {
    public:
        //ConsumerExtend *consumer;
        VxlanTunnelOrch *vxlan_tunnel_orch;

        VxlanTunnelOrchTest()
        {
            m_configDb = make_shared<DBConnector>(CONFIG_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_applDb = make_shared<DBConnector>(APPL_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_stateDb = make_shared<DBConnector>(STATE_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
        }

        ~VxlanTunnelOrchTest()
        {
        }

        void SetUp()
        {
            ASSERT_TRUE(orchgent_stub.saiInit() == SAI_STATUS_SUCCESS);
            vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);
            //consumer = new ConsumerExtend(new ConsumerStateTable(m_configDb.get(), string(CFG_VXLAN_TUNNEL_TABLE_NAME), 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME);
        }

        void TearDown()
        {
            //delete consumer;
            //delete vxlan_tunnel_orch;
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
        }

        void doVxlanTunnelOrchTask(const deque<KeyOpFieldsValuesTuple>& entries, const string command = SET_COMMAND)
        {
            //auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME));
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME)); 
            
            deque<KeyOpFieldsValuesTuple> tmp(entries);

            for (auto it = tmp.begin(); it != tmp.end(); ++it)
            {
                get<1>(*it) = command;
            }

            consumerAddToSync(consumer.get(), tmp);
            ((Orch *) vxlan_tunnel_orch)->doTask(*consumer.get());
        }

    };

    TEST_F(VxlanTunnelOrchTest, CreateNextHopWhenTunnelNonExist)
    {
        sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
        tunnelEndpoint endp;
        string exist_tunnel_name = "tunnel1";
        string non_exist_tunnel_name = "tunnel2";
        auto exist_tunnel_obj = VxlanTunnel(exist_tunnel_name, IpAddress("1.1.1.1"), IpAddress("2.2.2.2"));
        ASSERT_TRUE(exist_tunnel_obj.createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID));

        auto setData = deque<KeyOpFieldsValuesTuple>(
                { { exist_tunnel_name,
                    SET_COMMAND,
                    {
                      {"src_ip", "91.91.91.92"},
                      {"dst_ip", "101.101.101.102"}
                    }
                } });
        doVxlanTunnelOrchTask(setData);

        endp.ip = IpAddress("10.10.10.10");
        endp.mac = MacAddress("52:54:00:25:12:E9");
        endp.vni = 10000;
        nh_id = vxlan_tunnel_orch->createNextHopTunnel(non_exist_tunnel_name, endp.ip, endp.mac, endp.vni);

        /* Validate in local memory */
        ASSERT_EQ (nh_id, SAI_NULL_OBJECT_ID);

        doVxlanTunnelOrchTask(setData, DEL_COMMAND);
    }

    TEST_F(VxlanTunnelOrchTest, CreateNextHopWhenTunnelExist)
    {
        sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
        tunnelEndpoint endp;
        string exist_tunnel_name = "tunnel1";
        auto setData = deque<KeyOpFieldsValuesTuple>(
                { { exist_tunnel_name,
                    SET_COMMAND,
                    {
                      {"src_ip", "91.91.91.92"},
                      {"dst_ip", "101.101.101.102"}
                    }
                } });
        doVxlanTunnelOrchTask(setData);
        auto exist_tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(exist_tunnel_name);
        ASSERT_TRUE(!exist_tunnel_obj->isActive());
        ASSERT_TRUE(exist_tunnel_obj->createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID));
        ASSERT_TRUE(exist_tunnel_obj->isActive());

        endp.ip = IpAddress("10.10.10.10");
        endp.mac = MacAddress("52:54:00:25:12:E9");
        endp.vni = 10000;

        nh_id = vxlan_tunnel_orch->createNextHopTunnel(exist_tunnel_name, endp.ip, endp.mac, endp.vni);

        /* Validate in local memory */
        ASSERT_TRUE(nh_id != SAI_NULL_OBJECT_ID);
        ASSERT_TRUE(nh_id == exist_tunnel_obj->getNextHop(endp.ip, endp.mac, endp.vni));

        /* Validate in SAI */
        auto tunnel_id = exist_tunnel_obj->getTunnelId();
        sai_ip_address_t host_ip;
        copy(host_ip, endp.ip);
        sai_attribute_t attr;
        vector<sai_attr_id_t> validated_attr_ids = {SAI_NEXT_HOP_ATTR_TYPE,
                                        SAI_NEXT_HOP_ATTR_IP,
                                        SAI_NEXT_HOP_ATTR_TUNNEL_ID};

        if (endp.vni != 0) {
            validated_attr_ids.push_back(SAI_NEXT_HOP_ATTR_TUNNEL_VNI);
        }

        if (endp.mac) {
            validated_attr_ids.push_back(SAI_NEXT_HOP_ATTR_TUNNEL_MAC);
        }

        for (const sai_attr_id_t& attr_id : validated_attr_ids) {
            attr.id = attr_id;
            ASSERT_TRUE(SAI_STATUS_SUCCESS == sai_next_hop_api->get_next_hop_attribute(nh_id, 1, &attr));
            switch(attr.id) {
                case SAI_NEXT_HOP_ATTR_TYPE:
                    ASSERT_TRUE(attr.value.s32 == SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP);
                    break;

                case SAI_NEXT_HOP_ATTR_IP:
                    ASSERT_TRUE(attr.value.ipaddr.addr_family == SAI_IP_ADDR_FAMILY_IPV4);
                    ASSERT_TRUE(attr.value.ipaddr.addr.ip4 == host_ip.addr.ip4);
                    break;

                case SAI_NEXT_HOP_ATTR_TUNNEL_ID:
                    ASSERT_TRUE(attr.value.oid == tunnel_id);
                    break;

                case SAI_NEXT_HOP_ATTR_TUNNEL_VNI:
                    ASSERT_TRUE(attr.value.u32 == endp.vni);
                    break;

                case SAI_NEXT_HOP_ATTR_TUNNEL_MAC:
                    ASSERT_TRUE(memcmp(attr.value.mac, endp.mac.getMac(), ETHER_ADDR_LEN) == 0);
                    break;

                default:
                    break;
            }

        }

        /* Remove Test */
        ASSERT_TRUE(vxlan_tunnel_orch->removeNextHopTunnel(exist_tunnel_name, endp.ip, endp.mac, endp.vni));

        /* Validate in SAI */
        for (const sai_attr_id_t& attr_id : validated_attr_ids)
        {
            attr.id = attr_id;
            ASSERT_TRUE(SAI_STATUS_SUCCESS != sai_next_hop_api->get_next_hop_attribute(nh_id, 1, &attr));
        }
        doVxlanTunnelOrchTask(setData, DEL_COMMAND);
    }

    TEST_F(VxlanTunnelOrchTest, createVxlanTunnelMapExist)
    {
        sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
        string exist_tunnel_name = "tunnel1";
        uint32_t vni = 10000;
        auto setData = deque<KeyOpFieldsValuesTuple>(
                { { exist_tunnel_name,
                    SET_COMMAND,
                    {
                      {"src_ip", "91.91.91.92"},
                      {"dst_ip", "101.101.101.102"}
                    }
                } });
        doVxlanTunnelOrchTask(setData);
        auto exist_tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(exist_tunnel_name);
        ASSERT_TRUE(!exist_tunnel_obj->isActive());
        ASSERT_TRUE(exist_tunnel_obj->createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID));
        ASSERT_TRUE(exist_tunnel_obj->isActive());

        sai_object_id_t encapId, decapId;
        ASSERT_EQ(SAI_STATUS_SUCCESS, orchgent_stub.createVirtualRouter(encapId));
        ASSERT_EQ(SAI_STATUS_SUCCESS, orchgent_stub.createVirtualRouter(decapId));
        ASSERT_TRUE(vxlan_tunnel_orch->createVxlanTunnelMap(exist_tunnel_name, TUNNEL_MAP_T_VIRTUAL_ROUTER,
                                                            vni, encapId, decapId));

        /* Validate in local memory */
        const auto encap_tunnel_map_id = exist_tunnel_obj->tunnel_map_entries_[vni].first;
        const auto decap_tunnel_map_id = exist_tunnel_obj->tunnel_map_entries_[vni].second;
        ASSERT_NE(encap_tunnel_map_id, SAI_NULL_OBJECT_ID);
        ASSERT_NE(decap_tunnel_map_id, SAI_NULL_OBJECT_ID);

        const auto tunnel_encap_id = exist_tunnel_obj->getEncapMapId();
        const auto tunnel_decap_id = exist_tunnel_obj->getDecapMapId();
        ASSERT_NE(tunnel_encap_id, SAI_NULL_OBJECT_ID);
        ASSERT_NE(tunnel_decap_id, SAI_NULL_OBJECT_ID);

        /* Validate in SAI */
        sai_attribute_t attr;
        vector<bool> validated_is_encap = {true, false};
        for (const bool& is_encap : validated_is_encap) {
            vector<sai_attr_id_t> validated_attr_ids = { SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE,
                                                              SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP };

            const auto validated_obj_id = (is_encap)?encap_tunnel_map_id:decap_tunnel_map_id;
            const auto map_t = (is_encap)?exist_tunnel_obj->tunnel_map_.first: exist_tunnel_obj->tunnel_map_.second;
            validated_attr_ids.push_back(tunnel_map_key(map_t));
            validated_attr_ids.push_back(tunnel_map_val(map_t));

            for (const sai_attr_id_t& attr_id : validated_attr_ids) {
                attr.id = attr_id;
                ASSERT_EQ(SAI_STATUS_SUCCESS, sai_tunnel_api->get_tunnel_map_entry_attribute(validated_obj_id, 1, &attr));
                switch(attr.id) {
                    case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE:
                        ASSERT_EQ(attr.value.s32, tunnel_map_type(map_t));
                        break;

                    case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP:
                        if(is_encap) {
                            ASSERT_EQ(attr.value.oid, tunnel_encap_id);
                        }
                        else {
                            ASSERT_EQ(attr.value.oid, tunnel_decap_id);
                        }
                        break;
                }

                if(attr.id == tunnel_map_key(map_t)) {
                    if(is_encap) {
                        if(encapId != SAI_NULL_OBJECT_ID ) {
                            ASSERT_EQ(attr.value.oid, encapId);
                        }
                        else {
                            ASSERT_EQ(attr.value.u16, 0);
                        }
                    }
                    else {
                        ASSERT_EQ(attr.value.u32, vni);
                    }
                }

                if(attr.id == tunnel_map_val(map_t)) {
                    if(is_encap) {
                        ASSERT_EQ(attr.value.u32, vni);
                    }
                    else {
                        if(decapId != SAI_NULL_OBJECT_ID ) {
                            ASSERT_EQ(attr.value.oid, decapId);
                        }
                        else {
                            ASSERT_EQ(attr.value.u16, 0);
                        }
                    }
                }
            }
        }

        ASSERT_TRUE(vxlan_tunnel_orch->removeVxlanTunnelMap(exist_tunnel_name, vni));
        doVxlanTunnelOrchTask(setData, DEL_COMMAND);
    }

    class VxlanTunnelMapOrchTest : public Test
    {
    public:
        //ConsumerExtend *consumer;
        VxlanTunnelMapOrch *vxlan_tunnel_map_orch;
        VxlanTunnelOrch *vxlan_tunnel_orch;

        VxlanTunnelMapOrchTest()
        {
            m_configDb = make_shared<DBConnector>(CONFIG_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_applDb = make_shared<DBConnector>(APPL_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_stateDb = make_shared<DBConnector>(STATE_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
        }

        ~VxlanTunnelMapOrchTest()
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
            gPortsOrch = new PortsOrch(m_applDb.get(), ports_tables);

            vxlan_tunnel_map_orch = new VxlanTunnelMapOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_MAP_TABLE_NAME);

            if (!gDirectory.get<VxlanTunnelOrch*>())
            {
                vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
            }

            //consumer = new ConsumerExtend(new ConsumerStateTable(m_configDb.get(), string(CFG_VXLAN_TUNNEL_MAP_TABLE_NAME), 1, 1), vxlan_tunnel_map_orch, CFG_VXLAN_TUNNEL_MAP_TABLE_NAME);
        }

        void TearDown()
        {
            //delete consumer;
            delete vxlan_tunnel_map_orch;
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
        }

        void doVxlanTunnelMapOrchTask(const deque<KeyOpFieldsValuesTuple>& entries, const string command = SET_COMMAND)
        {
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_MAP_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vxlan_tunnel_map_orch, CFG_VXLAN_TUNNEL_MAP_TABLE_NAME));
            deque<KeyOpFieldsValuesTuple> tmp(entries);

            for (auto it = tmp.begin(); it != tmp.end(); ++it)
            {
                get<1>(*it) = command;
            }

            consumerAddToSync(consumer.get(), tmp);
            ((Orch *) vxlan_tunnel_map_orch)->doTask(*consumer.get());
        }

        void createTunnelInTunnelorch(string tunnel_name)
        {
            //ConsumerExtend *vxlan_tunnel_consumer = new ConsumerExtend(new ConsumerStateTable(m_configDb.get(), string(CFG_VXLAN_TUNNEL_TABLE_NAME), 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME);
            auto vxlan_tunnel_consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, 1, 1), vxlan_tunnel_orch, CFG_VXLAN_TUNNEL_TABLE_NAME));
            auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_name,
                        SET_COMMAND,
                        {
                          {"src_ip", "91.91.91.92"},
                          {"dst_ip", "101.101.101.102"}
                        }
                    } });

            consumerAddToSync(vxlan_tunnel_consumer.get(), setData);
            ((Orch *) vxlan_tunnel_orch)->doTask(*vxlan_tunnel_consumer.get());
            VxlanTunnel tunnel1 = VxlanTunnel(tunnel_name, IpAddress("91.91.91.92"), IpAddress("101.101.101.102"));
            ASSERT_TRUE(tunnel1.createTunnel(MAP_T::VRID_TO_VNI, MAP_T::VNI_TO_VRID));
            //delete vxlan_tunnel_consumer;

        }
    };

    TEST_F(VxlanTunnelMapOrchTest, TunnelMapCreation)
    {
        sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
        uint16_t vlan_id = 10;
        string vlan_alias = "Vlan" + to_string(vlan_id);
        string exist_tunnel_name = "tunnel1";
        string tunnel_map_name = exist_tunnel_name+":map1";

        createTunnelInTunnelorch(exist_tunnel_name);
        orchgent_stub.createVlanInPortorch(vlan_id);

        auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_map_name,
                        SET_COMMAND,
                        {
                          {"vni", "32768"},
                          {"vlan", vlan_alias}
                        }
                    } });
        doVxlanTunnelMapOrchTask(setData);

        /* Validate in local memory */
        ASSERT_TRUE(vxlan_tunnel_map_orch->vxlan_tunnel_map_table_.find(tunnel_map_name)!= vxlan_tunnel_map_orch->vxlan_tunnel_map_table_.end());
        ASSERT_TRUE(vxlan_tunnel_map_orch->vxlan_tunnel_map_table_.at(tunnel_map_name)!= SAI_NULL_OBJECT_ID);

        /* Validate in SAI */
        const auto tunnel_map_oid = vxlan_tunnel_map_orch->vxlan_tunnel_map_table_[tunnel_map_name];
        sai_attribute_t attr;
        vector<sai_attr_id_t> validated_attr_ids = { SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE,
                                                          SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP };

        for (const sai_attr_id_t& attr_id : validated_attr_ids) {
            attr.id = attr_id;
            ASSERT_EQ(SAI_STATUS_SUCCESS, sai_tunnel_api->get_tunnel_map_entry_attribute(tunnel_map_oid, 1, &attr));

            switch(attr.id) {
                case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE:
                    ASSERT_EQ(attr.value.s32, tunnel_map_type(MAP_T::VNI_TO_VLAN_ID));
                    break;

                case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP:
                    const auto tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(exist_tunnel_name);
                    ASSERT_EQ(attr.value.oid, tunnel_obj->getDecapMapId());
                    break;
            }

        }

        doVxlanTunnelMapOrchTask(setData, DEL_COMMAND);
    }

    class VxlanVrfMapOrchTest : public Test
    {
    public:
        //ConsumerExtend *consumer;
        VxlanVrfMapOrch *vxlan_vrf_orch;
        VxlanTunnelOrch *vxlan_tunnel_orch;
        VRFOrch *vrf_orch;

        VxlanVrfMapOrchTest()
        {
            m_configDb = make_shared<DBConnector>(CONFIG_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_applDb = make_shared<DBConnector>(APPL_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_stateDb = make_shared<DBConnector>(STATE_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
        }

        ~VxlanVrfMapOrchTest()
        {
        }

        void SetUp()
        {
            ASSERT_TRUE(orchgent_stub.saiInit() == SAI_STATUS_SUCCESS);
            vxlan_vrf_orch = new VxlanVrfMapOrch(m_applDb.get(), APP_VXLAN_VRF_TABLE_NAME);
            //vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);

            vrf_orch = gDirectory.get<VRFOrch*>();
            if (!vrf_orch)
            {
                vrf_orch = new VRFOrch(m_applDb.get(), APP_VRF_TABLE_NAME);
                gDirectory.set(vrf_orch);
            }

            vxlan_tunnel_orch = gDirectory.get<VxlanTunnelOrch*>();
            if (!vxlan_tunnel_orch)
            {
                vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
            }

            //consumer = new ConsumerExtend(new ConsumerStateTable(m_applDb.get(), string(APP_VXLAN_VRF_TABLE_NAME), 1, 1), vxlan_vrf_orch, APP_VXLAN_VRF_TABLE_NAME);
        }

        void TearDown()
        {
            //delete consumer;
            delete vxlan_vrf_orch;
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
        }

        void doVxlanVrfMapOrchTask(const deque<KeyOpFieldsValuesTuple>& entries, const string command = SET_COMMAND)
        {
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VXLAN_VRF_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vxlan_vrf_orch, APP_VXLAN_VRF_TABLE_NAME));
            deque<KeyOpFieldsValuesTuple> tmp(entries);

            for (auto it = tmp.begin(); it != tmp.end(); ++it)
            {
                get<1>(*it) = command;
            }

            consumerAddToSync(consumer.get(), tmp);
            ((Orch *) vxlan_vrf_orch)->doTask(*consumer.get());
        }
    };

    TEST_F(VxlanVrfMapOrchTest, VrfMapCreationWhenVrfExist)
    {
        string exist_tunnel_name = "tunnel1";
        string exist_vrf_name = "existVRF";
        uint32_t vni = 32768;
        orchgent_stub.createVrfInVrforch(exist_vrf_name);
        string tunnel_map_name = exist_tunnel_name+":map1";
        orchgent_stub.createTunnelInTunnelorch(exist_tunnel_name);
        //auto vrf_id = vrf_orch->getVRFid(exist_vrf_name);
        auto exist_tunnel_obj = vxlan_tunnel_orch->getVxlanTunnel(exist_tunnel_name);
        auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_map_name,
                        SET_COMMAND,
                        {
                          {"vni", to_string(vni)},
                          {"vrf", exist_vrf_name}
                        }
                    } });
        doVxlanVrfMapOrchTask(setData);

        /* Validate in local memory */
        ASSERT_TRUE(vxlan_vrf_orch->vxlan_vrf_table_.find(tunnel_map_name)!= vxlan_vrf_orch->vxlan_vrf_table_.end());
        ASSERT_TRUE(vxlan_vrf_orch->vxlan_vrf_tunnel_.at(exist_vrf_name)!= SAI_NULL_OBJECT_ID);
        auto vrf_entry = vxlan_vrf_orch->vxlan_vrf_table_[tunnel_map_name];
        const auto encap_tunnel_map_id = vrf_entry.encap_id;
        const auto decap_tunnel_map_id = vrf_entry.decap_id;
        ASSERT_NE(encap_tunnel_map_id, SAI_NULL_OBJECT_ID);
        ASSERT_NE(decap_tunnel_map_id, SAI_NULL_OBJECT_ID);

        const auto tunnel_encap_id = exist_tunnel_obj->getEncapMapId();
        const auto tunnel_decap_id = exist_tunnel_obj->getDecapMapId();
        ASSERT_NE(tunnel_encap_id, SAI_NULL_OBJECT_ID);
        ASSERT_NE(tunnel_decap_id, SAI_NULL_OBJECT_ID);

        doVxlanVrfMapOrchTask(setData, DEL_COMMAND);
    }

    TEST_F(VxlanVrfMapOrchTest, VrfMapCreationWhenVrfNotExist)
    {
        string exist_tunnel_name = "tunnel1";
        string tunnel_map_name = exist_tunnel_name+":map1";
        string non_exist_vrf_name = "nonexistVRF";
        orchgent_stub.createTunnelInTunnelorch(exist_tunnel_name);
        auto setData = deque<KeyOpFieldsValuesTuple>(
                    { { tunnel_map_name,
                        SET_COMMAND,
                        {
                          {"vni", "32768"},
                          {"vrf", non_exist_vrf_name}
                        }
                    } });
        doVxlanVrfMapOrchTask(setData);

        /* Validate in local memory */
        ASSERT_TRUE(vxlan_vrf_orch->vxlan_vrf_table_.find(tunnel_map_name) == vxlan_vrf_orch->vxlan_vrf_table_.end());

        doVxlanVrfMapOrchTask(setData, DEL_COMMAND);
    }

}

