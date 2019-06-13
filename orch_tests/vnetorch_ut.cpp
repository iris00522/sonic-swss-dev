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

    class VnetOrchTest : public Test
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
            delete vxlan_tunnel_orch;
            delete vrf_orch;
            delete vnet_rt_orch;
            delete vnet_orch;
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
            }
            
            if(!gDirectory.get<VNetRouteOrch*>()){
                vnet_rt_orch = new VNetRouteOrch(m_applDb.get(), vnet_tables, vnet_orch);
                gDirectory.set(vnet_rt_orch);
            }
                        
            if(!gDirectory.get<VRFOrch*>()){
                vrf_orch = new VRFOrch(m_applDb.get(), APP_VRF_TABLE_NAME);
                gDirectory.set(vrf_orch);
            }
            
            gIntfsOrch = new IntfsOrch(m_applDb.get(), APP_INTF_TABLE_NAME, vrf_orch);

            if(!gDirectory.get<VxlanTunnelOrch*>()){
                vxlan_tunnel_orch = new VxlanTunnelOrch(m_applDb.get(), APP_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
            }

        }

        void TearDown()
        {
            delete gPortsOrch;
            delete gIntfsOrch;
            ASSERT_TRUE(orchgent_stub.saiUnInit() == SAI_STATUS_SUCCESS);
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
        
        bool create_vnet_routes(string prefix, string vnet_name, string endpoint, string mac="",int vni=0)
        {
            auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_applDb.get(), APP_VNET_RT_TUNNEL_TABLE_NAME, 1, 1), vnet_rt_orch, APP_VNET_RT_TUNNEL_TABLE_NAME));
            string name = vnet_name + ":" + prefix;

            vector<FieldValueTuple> setData =
            { 
                {"endpoint",  "Vnet_2000"},
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
    TEST_F(VnetOrchTest, Vnet_Creation)
    {
        string tunnel_name = "tunnel_1";

        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "10.10.10.10") == true);
        ASSERT_TRUE(create_vnet_entry("Vnet_2000", tunnel_name, "2000", "")== true);
        ASSERT_TRUE(create_vlan_interface(100, "Ethernet24", "Vnet_2000", "100.100.3.1/24")== true);
        ASSERT_TRUE(create_vlan_interface(101, "Ethernet28", "Vnet_2000", "100.100.4.1/24")== true);
        ASSERT_TRUE(create_vnet_routes("100.100.1.1/32", "Vnet_2000", "10.10.10.1")== true);
        ASSERT_TRUE(create_vnet_local_routes("100.100.3.0/24", "Vnet_2000", "Vlan100")== true);
        ASSERT_TRUE(create_vnet_local_routes("100.100.4.0/24", "Vnet_2000", "Vlan101")== true);

        //Create Physical Interface in another Vnet
        ASSERT_TRUE(create_vnet_entry("Vnet_2001", tunnel_name, "2001", "")== true);
        ASSERT_TRUE(create_vlan_interface(100, "Ethernet4", "Vnet_2001", "100.102.1.1/24")== true);
        ASSERT_TRUE(create_vnet_routes("100.100.2.1/32", "Vnet_2001", "10.10.10.2", "00:12:34:56:78:9A")== true);
        ASSERT_TRUE(create_vnet_local_routes("100.102.1.0/24", "Vnet_2001", "Ethernet4")== true);  

    }
    
    //Test 2 - Two VNets, One HSMs per VNet

    TEST_F(VnetOrchTest,test_vnet_orch_2)
    {
        string tunnel_name = "tunnel_2";
        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "6.6.6.6") == true);
        ASSERT_TRUE(create_vlan_interface(1001, "Ethernet0", "Vnet_1", "1.1.10.1/24")== true);
        ASSERT_TRUE(create_vnet_routes("1.1.1.10/32", "Vnet_1", "100.1.1.10")== true);
    
        ASSERT_TRUE(create_vnet_routes("1.1.1.11/32", "Vnet_1", "100.1.1.10")== true);
    
        ASSERT_TRUE(create_vnet_routes("1.1.1.12/32", "Vnet_1", "200.200.1.200")== true);
    
        ASSERT_TRUE(create_vnet_routes("1.1.1.14/32", "Vnet_1", "200.200.1.201")== true);
    
        ASSERT_TRUE(create_vnet_local_routes("1.1.10.0/24", "Vnet_1", "Vlan1001")== true);
        ASSERT_TRUE(create_vnet_entry("Vnet_2", tunnel_name, "2222", "")== true);
    
        ASSERT_TRUE(create_vlan_interface(1002, "Ethernet4", "Vnet_2", "2.2.10.1/24")== true);
        ASSERT_TRUE(create_vnet_routes("2.2.2.10/32", "Vnet_2", "100.1.1.20")== true);
    
        ASSERT_TRUE(create_vnet_routes("2.2.2.11/32", "Vnet_2", "100.1.1.20")== true);
        ASSERT_TRUE(create_vnet_local_routes( "2.2.10.0/24", "Vnet_2", "Vlan1002")== true);
    }


    //Test 3 - Two VNets, One HSMs per VNet, Peering
    TEST_F(VnetOrchTest,test_vnet_orch_3)
    {

        string tunnel_name = "tunnel_3";

        ASSERT_TRUE(create_vxlan_tunnel(tunnel_name, "7.7.7.7")== true);

        ASSERT_TRUE(create_vnet_entry("Vnet_10", tunnel_name, "3333", "Vnet_20")== true);

        ASSERT_TRUE(create_vnet_entry("Vnet_20", tunnel_name, "4444", "Vnet_10")== true);
        ASSERT_TRUE(create_vlan_interface(2001, "Ethernet8", "Vnet_10", "5.5.10.1/24")== true);

        ASSERT_TRUE(create_vlan_interface(2002, "Ethernet12", "Vnet_20", "8.8.10.1/24")== true);

        ASSERT_TRUE(create_vnet_routes("5.5.5.10/32", "Vnet_10", "50.1.1.10")== true);

        ASSERT_TRUE(create_vnet_routes( "8.8.8.10/32", "Vnet_20", "80.1.1.20")== true);

        ASSERT_TRUE(create_vnet_local_routes("5.5.10.0/24", "Vnet_10", "Vlan2001")== true);

        ASSERT_TRUE(create_vnet_local_routes("8.8.10.0/24", "Vnet_20", "Vlan2002")== true);
    }
}

