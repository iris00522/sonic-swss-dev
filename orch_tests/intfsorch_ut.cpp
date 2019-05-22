#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <string>
#include <iostream>
#include <vector>
#include "orch.h"
#include "directory.h"
#include "switchorch.h"
#include "portsorch.h"
#include "bufferorch.h"
#define private public
#define protected public
#include "intfsorch.h"
#include "crmorch.h"
#undef private
#undef protected
#include "routeorch.h"
#include "neighorch.h"
#include "vnetorch.h"
#include "vrforch.h"
#include "vxlanorch.h"
#include "dbconnector.h"
#include "port.h"
#include "saihelper.h"
#include "saiattributelist.h"
#include "subscriberstatetable.h"
#include "ipprefix.h"
#include "tokenize.h"
#include "swssnet.h"

using namespace std;
using namespace swss;

#define FRONT_PANEL_PORT_PREFIX "Ethernet"

extern int gBatchSize;
extern bool gSwssRecord;
extern bool gSairedisRecord;
extern bool gLogRotate;
extern ofstream gRecordOfs;
extern string gRecordFile;

extern MacAddress gMacAddress;
extern MacAddress gVxlanMacAddress;

extern sai_object_id_t gVirtualRouterId;
extern sai_object_id_t gUnderlayIfId;
extern sai_object_id_t gSwitchId;

extern Directory<Orch*> gDirectory;

extern sai_switch_api_t* sai_switch_api;
extern sai_bridge_api_t* sai_bridge_api;
extern sai_virtual_router_api_t* sai_virtual_router_api;
extern sai_port_api_t* sai_port_api;
extern sai_vlan_api_t* sai_vlan_api;
extern sai_router_interface_api_t* sai_router_intfs_api;
extern sai_route_api_t* sai_route_api;
extern sai_neighbor_api_t* sai_neighbor_api;
extern sai_tunnel_api_t* sai_tunnel_api;
extern sai_next_hop_api_t* sai_next_hop_api;

extern SwitchOrch *gSwitchOrch;
extern PortsOrch *gPortsOrch;
extern IntfsOrch *gIntfsOrch;
extern NeighOrch *gNeighOrch;
extern RouteOrch *gRouteOrch;
extern CrmOrch *gCrmOrch;
extern BufferOrch *gBufferOrch;
extern VRFOrch *gVrfOrch;


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

class IntfsOrchTest : public ::testing::Test
{
    public:
        bool createAllBridgePorts();
        void doIntfsOrchTask(const deque<KeyOpFieldsValuesTuple>& entries, const string command = SET_COMMAND);
        bool createVRF(const string vrf_name);
        bool deleteVRF(const string vrf_name);
        bool createVlan(string vlan_name);
        bool addVlanMember(string vlan_name, string phy_name);
        bool createVxlan(string tunnel_name);
        bool createVnet(string vnet_name, uint32_t vni, string vxlan_name);
        sai_object_id_t getRouterInterfaceId(string alias);
        sai_object_id_t getVirtualRouterId(sai_object_id_t rif_id);
        sai_object_id_t getVirtualRouterId(string alias);
        sai_object_id_t getCpuPortId();
        bool isRouteExist(sai_object_id_t vr_id, IpPrefix p);
        int getRoutePacketAction(sai_object_id_t vr_id, IpPrefix p);
        sai_object_id_t getRouteNexthopId(sai_object_id_t vr_id, IpPrefix p);
        MacAddress getNeighborDstMac(string alias, IpAddress ip);
        uint32_t getRouterIntfsMtu(string alias);
        uint32_t getCrmResUsedCounter(CrmResourceType resource);

    protected:
        shared_ptr<DBConnector> m_configDb;
        shared_ptr<DBConnector> m_applDb;

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

        void SetUp() override {
            gProfileMap.emplace("SAI_VS_SWITCH_TYPE", "SAI_VS_SWITCH_TYPE_BCM56850");
            gProfileMap.emplace("KV_DEVICE_MAC_ADDRESS", "00:01:02:03:04:05");

            sai_service_method_table_t test_services = {
                IntfsOrchTest::profile_get_value,
                IntfsOrchTest::profile_get_next_value
            };

            // init sai
            // this api requires running redis db with enabled unix socket
            auto status = sai_api_initialize(0, (sai_service_method_table_t*)&test_services);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);
            sai_api_query(SAI_API_BRIDGE, (void**)&sai_bridge_api);
            sai_api_query(SAI_API_VIRTUAL_ROUTER, (void**)&sai_virtual_router_api);
            sai_api_query(SAI_API_PORT, (void**)&sai_port_api);
            sai_api_query(SAI_API_VLAN, (void**)&sai_vlan_api);
            sai_api_query(SAI_API_ROUTER_INTERFACE, (void**)&sai_router_intfs_api);
            sai_api_query(SAI_API_ROUTE, (void**)&sai_route_api);
            sai_api_query(SAI_API_NEIGHBOR, (void**)&sai_neighbor_api);
            sai_api_query(SAI_API_TUNNEL, (void**)&sai_tunnel_api);
            sai_api_query(SAI_API_NEXT_HOP, (void**)&sai_next_hop_api);

            // create switch
            sai_attribute_t attr;
            vector<sai_attribute_t> attrs;
            attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
            attr.value.booldata = true;
            attrs.push_back(attr);
            status = sai_switch_api->create_switch(&gSwitchId, (uint32_t)attrs.size(), attrs.data());
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            // Get switch source MAC address
            attr.id = SAI_SWITCH_ATTR_SRC_MAC_ADDRESS;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            gMacAddress = attr.value.mac;
            gVxlanMacAddress = attr.value.mac;

            // Get virtual router id
            attr.id = SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            gVirtualRouterId = attr.value.oid;

            // Create a loopback underlay router interface
            vector<sai_attribute_t> underlay_intf_attrs;

            sai_attribute_t underlay_intf_attr;
            underlay_intf_attr.id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
            underlay_intf_attr.value.oid = gVirtualRouterId;
            underlay_intf_attrs.push_back(underlay_intf_attr);

            underlay_intf_attr.id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
            underlay_intf_attr.value.s32 = SAI_ROUTER_INTERFACE_TYPE_LOOPBACK;
            underlay_intf_attrs.push_back(underlay_intf_attr);

            status = sai_router_intfs_api->create_router_interface(&gUnderlayIfId, gSwitchId, (uint32_t)underlay_intf_attrs.size(), underlay_intf_attrs.data());
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            // create db connector
            m_configDb = make_shared<swss::DBConnector>(CONFIG_DB, swss::DBConnector::DEFAULT_UNIXSOCKET, 0);
            m_applDb = make_shared<swss::DBConnector>(APPL_DB, swss::DBConnector::DEFAULT_UNIXSOCKET, 0);

            //create all required orchs
            ASSERT_EQ(gSwitchOrch, nullptr);
            gSwitchOrch = new SwitchOrch(m_applDb.get(), APP_SWITCH_TABLE_NAME);
            ASSERT_EQ(gCrmOrch, nullptr);
            gCrmOrch = new CrmOrch(m_configDb.get(), CFG_CRM_TABLE_NAME);

            const int portsorch_base_pri = 40;
            vector<table_name_with_pri_t> ports_tables = {
                { APP_PORT_TABLE_NAME,        portsorch_base_pri + 5 },
                { APP_VLAN_TABLE_NAME,        portsorch_base_pri + 2 },
                { APP_VLAN_MEMBER_TABLE_NAME, portsorch_base_pri     },
                { APP_LAG_TABLE_NAME,         portsorch_base_pri + 4 },
                { APP_LAG_MEMBER_TABLE_NAME,  portsorch_base_pri     }
            };

            ASSERT_EQ(gPortsOrch, nullptr);
            gPortsOrch = new PortsOrch(m_applDb.get(), ports_tables);

            vector<string> buffer_tables = {
                CFG_BUFFER_POOL_TABLE_NAME,
                CFG_BUFFER_PROFILE_TABLE_NAME,
                CFG_BUFFER_QUEUE_TABLE_NAME,
                CFG_BUFFER_PG_TABLE_NAME,
                CFG_BUFFER_PORT_INGRESS_PROFILE_LIST_NAME,
                CFG_BUFFER_PORT_EGRESS_PROFILE_LIST_NAME
            };

            ASSERT_EQ(gBufferOrch, nullptr);
            gBufferOrch = new BufferOrch(m_configDb.get(), buffer_tables);

            if (!gDirectory.get<VNetOrch*>())
            {
                VNetOrch *vnet_orch = new VNetOrch(m_applDb.get(), APP_VNET_TABLE_NAME);
                gDirectory.set(vnet_orch);
            }

            if (!gDirectory.get<VNetRouteOrch*>())
            {
                vector<string> vnet_tables = {
                    APP_VNET_RT_TABLE_NAME,
                    APP_VNET_RT_TUNNEL_TABLE_NAME
                };
                VNetOrch *vnet_orch = gDirectory.get<VNetOrch*>();
                VNetRouteOrch *vnet_rt_orch = new VNetRouteOrch(m_applDb.get(), vnet_tables, vnet_orch);
                gDirectory.set(vnet_rt_orch);
            }

            if (!gDirectory.get<VxlanTunnelOrch*>())
            {
                VxlanTunnelOrch *vxlan_tunnel_orch = new VxlanTunnelOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_orch);
                VxlanTunnelMapOrch *vxlan_tunnel_map_orch = new VxlanTunnelMapOrch(m_configDb.get(), CFG_VXLAN_TUNNEL_MAP_TABLE_NAME);
                gDirectory.set(vxlan_tunnel_map_orch);
                VxlanVrfMapOrch *vxlan_vrf_map_orch = new VxlanVrfMapOrch(m_applDb.get(), APP_VXLAN_VRF_TABLE_NAME);
                gDirectory.set(vxlan_vrf_map_orch);
            }

            if (!(gVrfOrch = gDirectory.get<VRFOrch*>()))
            {
                gVrfOrch = new VRFOrch(m_applDb.get(), APP_VRF_TABLE_NAME);
                gDirectory.set(gVrfOrch);
            }

            ASSERT_EQ(gIntfsOrch, nullptr);
            gIntfsOrch = new IntfsOrch(m_applDb.get(), APP_INTF_TABLE_NAME, gVrfOrch);
            ASSERT_EQ(gNeighOrch, nullptr);
            gNeighOrch = new NeighOrch(m_applDb.get(), APP_NEIGH_TABLE_NAME, gIntfsOrch);
            ASSERT_EQ(gRouteOrch, nullptr);
            gRouteOrch = new RouteOrch(m_applDb.get(), APP_ROUTE_TABLE_NAME, gNeighOrch);

            // init portsorch
            auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_PORT_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gPortsOrch, APP_PORT_TABLE_NAME));
            consumerAddToSync(consumer.get(), { { "PortInitDone", EMPTY_PREFIX, {} } });
            ((Orch *) gPortsOrch)->doTask(*consumer.get());
            ASSERT_TRUE(gPortsOrch->isInitDone());

            ASSERT_TRUE(createAllBridgePorts());
        }

        void TearDown() override
        {
            delete gSwitchOrch;
            gSwitchOrch = nullptr;
            delete gPortsOrch;
            gPortsOrch = nullptr;
            delete gIntfsOrch;
            gIntfsOrch = nullptr;
            delete gNeighOrch;
            gNeighOrch = nullptr;
            delete gRouteOrch;
            gRouteOrch = nullptr;
            delete gCrmOrch;
            gCrmOrch = nullptr;
            delete gBufferOrch;
            gBufferOrch = nullptr;

            sai_status_t status = sai_switch_api->remove_switch(gSwitchId);
            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);
            gSwitchId = 0;

            sai_api_uninitialize();

            sai_switch_api = nullptr;
            sai_bridge_api = nullptr;
            sai_virtual_router_api = nullptr;
            sai_port_api = nullptr;
            sai_vlan_api = nullptr;
            sai_router_intfs_api = nullptr;
            sai_route_api = nullptr;
            sai_neighbor_api = nullptr;
            sai_tunnel_api = nullptr;
            sai_next_hop_api = nullptr;
        }
};

map<string, string> IntfsOrchTest::gProfileMap;
map<string, string>::iterator IntfsOrchTest::gProfileIter = IntfsOrchTest::gProfileMap.begin();

void IntfsOrchTest::doIntfsOrchTask(const deque<KeyOpFieldsValuesTuple>& entries, const string command)
{
    auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_INTF_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gIntfsOrch, APP_INTF_TABLE_NAME));
    deque<KeyOpFieldsValuesTuple> tmp(entries);

    for (auto it = tmp.begin(); it != tmp.end(); ++it)
    {
        get<1>(*it) = command;
    }

    consumerAddToSync(consumer.get(), tmp);
    ((Orch *) gIntfsOrch)->doTask(*consumer.get());
}

bool IntfsOrchTest::createAllBridgePorts()
{
    vector<sai_object_id_t> allPorts;
    sai_attribute_t attr;
    vector<sai_attribute_t> attrs;
    sai_status_t status;
    uint32_t port_count;
    int index = 0;

    attr.id = SAI_SWITCH_ATTR_PORT_NUMBER;
    status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
	return false;
    }
    port_count = attr.value.u32;

    allPorts.resize(port_count);
    attr.id = SAI_SWITCH_ATTR_PORT_LIST;
    attr.value.objlist.count = port_count;
    attr.value.objlist.list = allPorts.data();
    status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
    if (status != SAI_STATUS_SUCCESS || attr.value.objlist.count != port_count)
    {
        return false;
    }

    for (auto it = allPorts.begin(); it != allPorts.end(); ++it)
    {
        string name = FRONT_PANEL_PORT_PREFIX + to_string(++index);
        Port p = Port(name, Port::PHY);
        p.m_port_id = *it;

        attrs.clear();
        attr.id = SAI_BRIDGE_PORT_ATTR_TYPE;
        attr.value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;
        attrs.push_back(attr);

        attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        attr.value.oid = p.m_port_id;
        attrs.push_back(attr);

        /* Create a bridge port with admin status set to UP */
        attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
        attr.value.booldata = true;
        attrs.push_back(attr);

        /* And with hardware FDB learning mode set to HW (explicit default value) */
        attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
        attr.value.s32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW;
        attrs.push_back(attr);

        status = sai_bridge_api->create_bridge_port(&p.m_bridge_port_id, gSwitchId, (uint32_t)attrs.size(), attrs.data());
        if (status != SAI_STATUS_SUCCESS)
        {
            return false;
        }

        gPortsOrch->setPort(name, p);
    }

    return true;
}

bool IntfsOrchTest::createVRF(const string vrf_name)
{
    auto vrfData = deque<KeyOpFieldsValuesTuple>(
        { { vrf_name,
            SET_COMMAND,
            {{"v4", "true"},
             {"v6", "true"},
             {"src_mac", gMacAddress.to_string()},
             {"ttl_action", "trap"},
             {"ip_opt_action", "drop"},
             {"l3_mc_action", "forward"}}
        } });
    auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VRF_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gVrfOrch, APP_VRF_TABLE_NAME));

    consumerAddToSync(consumer.get(), vrfData);
    ((Orch *) gVrfOrch)->doTask(*consumer.get());

    return gVrfOrch->isVRFexists(vrf_name);
}

bool IntfsOrchTest::deleteVRF(const string vrf_name)
{
    auto vrfData = deque<KeyOpFieldsValuesTuple>(
        { { vrf_name,
            DEL_COMMAND,
            {}
        } });
    auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VRF_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), gVrfOrch, APP_VRF_TABLE_NAME));

    consumerAddToSync(consumer.get(), vrfData);
    ((Orch *) gVrfOrch)->doTask(*consumer.get());

    return !gVrfOrch->isVRFexists(vrf_name);
}

bool IntfsOrchTest::createVlan(string vlan_name)
{
    sai_attribute_t attr;
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    uint32_t vlan_id;

    sscanf(vlan_name.c_str(), "Vlan%u", &vlan_id);
    attr.id = SAI_VLAN_ATTR_VLAN_ID;
    attr.value.u16 = (sai_uint16_t) vlan_id;
    sai_status_t status = sai_vlan_api->create_vlan(&vlan_obj_id, gSwitchId, 1, &attr);
    if (status != SAI_STATUS_SUCCESS && status != SAI_STATUS_ITEM_ALREADY_EXISTS)
    {
        return false;
    }

    Port v = Port(vlan_name, Port::VLAN);
    v.m_vlan_info.vlan_oid = vlan_obj_id;
    v.m_vlan_info.vlan_id = (sai_vlan_id_t)vlan_id;
    gPortsOrch->setPort(vlan_name, v);

    return true;
}

bool IntfsOrchTest::addVlanMember(string vlan_name, string phy_name)
{
    sai_attribute_t attr;
    vector<sai_attribute_t> attr_list;
    sai_attribute_t member_attr;
    vector<sai_attribute_t> member_attrs;
    sai_object_id_t vlan_member_id;
    sai_status_t status;
    Port vlan, phy;

    if (!gPortsOrch->getPort(vlan_name, vlan) ||
        !gPortsOrch->getPort(phy_name, phy))
    {
        return false;
    }

    member_attr.id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
    member_attr.value.oid = vlan.m_vlan_info.vlan_oid;
    member_attrs.push_back(member_attr);

    member_attr.id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
    member_attr.value.oid =  phy.m_bridge_port_id;
    member_attrs.push_back(member_attr);

    member_attr.id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
    member_attr.value.s32 = SAI_VLAN_TAGGING_MODE_UNTAGGED;
    member_attrs.push_back(member_attr);

    status = sai_vlan_api->create_vlan_member(&vlan_member_id, gSwitchId, (uint32_t)member_attrs.size(), member_attrs.data());
    if (status != SAI_STATUS_SUCCESS)
    {
        return false;
    }

    attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;
    attr.value.u16 = vlan.m_vlan_info.vlan_id;

    status = sai_port_api->set_port_attribute(phy.m_port_id, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        return false;
    }

    return true;
}

bool IntfsOrchTest::createVxlan(string tunnel_name)
{
    VxlanTunnelOrch* vxlan_orch = gDirectory.get<VxlanTunnelOrch*>();
    auto consumer = unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_configDb.get(), CFG_VXLAN_TUNNEL_TABLE_NAME, 1, 1), vxlan_orch, CFG_VXLAN_TUNNEL_TABLE_NAME));
    auto vxlanData = deque<KeyOpFieldsValuesTuple>(
            { { tunnel_name,
                SET_COMMAND,
                {
                  {"src_ip", "91.91.91.92"},
                  {"dst_ip", "101.101.101.102"}
                }
            } });

    consumerAddToSync(consumer.get(), vxlanData);
    ((Orch *) vxlan_orch)->doTask(*consumer.get());

    return vxlan_orch->isTunnelExists(tunnel_name);
}

bool IntfsOrchTest::createVnet(string vnet_name, uint32_t vni, string vxlan_name)
{
    auto vnetData = deque<KeyOpFieldsValuesTuple>(
        { { vnet_name,
            SET_COMMAND,
            {{"src_mac", gMacAddress.to_string()},
             {"vni", to_string(vni)},
             {"vxlan_tunnel", vxlan_name}}
        } });
    VNetOrch* vnet_orch = gDirectory.get<VNetOrch*>();
    VxlanTunnelOrch* vxlan_orch = gDirectory.get<VxlanTunnelOrch*>();

    if (!vxlan_orch->isTunnelExists(vxlan_name))
    {
        createVxlan(vxlan_name);
    }

    auto consumer = unique_ptr<Consumer>(new Consumer(new SubscriberStateTable(m_applDb.get(), APP_VNET_TABLE_NAME, TableConsumable::DEFAULT_POP_BATCH_SIZE, 0), vnet_orch, APP_VNET_TABLE_NAME));
    consumerAddToSync(consumer.get(), vnetData);
    ((Orch *) vnet_orch)->doTask(*consumer.get());

    return vnet_orch->isVnetExists(vnet_name);
}

sai_object_id_t IntfsOrchTest::getRouterInterfaceId(string alias)
{
    Port p;

    if (!gPortsOrch->getPort(alias, p))
    {
        return SAI_NULL_OBJECT_ID;
    }

    return p.m_rif_id;
}

sai_object_id_t IntfsOrchTest::getVirtualRouterId(sai_object_id_t rif_id)
{
    if (rif_id == SAI_NULL_OBJECT_ID)
    {
        return SAI_NULL_OBJECT_ID;
    }

    // get virtual router id attribute
    sai_attribute_t attr;
    sai_status_t status;
    attr.id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
    status = sai_router_intfs_api->get_router_interface_attribute(rif_id, 1, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        return SAI_NULL_OBJECT_ID;
    }

    return attr.value.oid;
}

sai_object_id_t IntfsOrchTest::getVirtualRouterId(string alias)
{
    return getVirtualRouterId(getRouterInterfaceId(alias));
}

sai_object_id_t IntfsOrchTest::getCpuPortId()
{
    Port cpu_port;
    gPortsOrch->getCpuPort(cpu_port);

    return cpu_port.m_port_id;
}

bool IntfsOrchTest::isRouteExist(sai_object_id_t vr_id, IpPrefix p)
{
    sai_attribute_t attr;
    sai_route_entry_t unicast_route_entry;
    sai_status_t status;

    unicast_route_entry.switch_id = gSwitchId;
    unicast_route_entry.vr_id = vr_id;
    copy(unicast_route_entry.destination, p);
    subnet(unicast_route_entry.destination, unicast_route_entry.destination);

    attr.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    status = sai_route_api->get_route_entry_attribute(&unicast_route_entry, 1, &attr);

    return (status == SAI_STATUS_SUCCESS);
}

int IntfsOrchTest::getRoutePacketAction(sai_object_id_t vr_id, IpPrefix p)
{
    sai_attribute_t attr;
    sai_route_entry_t unicast_route_entry;
    sai_status_t status;

    unicast_route_entry.switch_id = gSwitchId;
    unicast_route_entry.vr_id = vr_id;
    copy(unicast_route_entry.destination, p);
    subnet(unicast_route_entry.destination, unicast_route_entry.destination);

    attr.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    status = sai_route_api->get_route_entry_attribute(&unicast_route_entry, 1, &attr);
    EXPECT_EQ(status, SAI_STATUS_SUCCESS);

    return attr.value.s32;
}

sai_object_id_t IntfsOrchTest::getRouteNexthopId(sai_object_id_t vr_id, IpPrefix p)
{
    sai_attribute_t attr;
    sai_route_entry_t unicast_route_entry;
    sai_status_t status;

    unicast_route_entry.switch_id = gSwitchId;
    unicast_route_entry.vr_id = vr_id;
    copy(unicast_route_entry.destination, p);
    subnet(unicast_route_entry.destination, unicast_route_entry.destination);

    attr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    status = sai_route_api->get_route_entry_attribute(&unicast_route_entry, 1, &attr);
    EXPECT_EQ(status, SAI_STATUS_SUCCESS);

    return attr.value.oid;
}

MacAddress IntfsOrchTest::getNeighborDstMac(string alias, IpAddress ip)
{
    sai_neighbor_entry_t neighbor_entry;
    sai_status_t status;

    neighbor_entry.rif_id = getRouterInterfaceId(alias);
    neighbor_entry.switch_id = gSwitchId;
    copy(neighbor_entry.ip_address, ip);

    sai_attribute_t neighbor_attr;
    neighbor_attr.id = SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS;
    status = sai_neighbor_api->get_neighbor_entry_attribute(&neighbor_entry, 1, &neighbor_attr);
    EXPECT_EQ(status, SAI_STATUS_SUCCESS);

    return MacAddress(neighbor_attr.value.mac);
}

uint32_t IntfsOrchTest::getRouterIntfsMtu(string alias)
{
    sai_attribute_t attr;
    attr.id = SAI_ROUTER_INTERFACE_ATTR_MTU;

    sai_status_t status = sai_router_intfs_api->get_router_interface_attribute(getRouterInterfaceId(alias), 1, &attr);
    EXPECT_EQ(status, SAI_STATUS_SUCCESS);

    return attr.value.u32;
}

uint32_t IntfsOrchTest::getCrmResUsedCounter(CrmResourceType resource)
{
    #define CRM_COUNTERS_TABLE_KEY "STATS" //defined in crmorch.cpp

    return gCrmOrch->m_resourcesMap.at(resource).countersMap[CRM_COUNTERS_TABLE_KEY].usedCounter;
}

TEST_F(IntfsOrchTest, addLoopbackInterfaceIPv4Subnet)
{
    uint32_t crm_ipv4_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE);

    // add subnet 192.168.1.1/24 to loopback interface (lo)
    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "lo:192.168.1.1/24",
              SET_COMMAND,
              {}
          } });
    doIntfsOrchTask(setData);

    // verify ip to me
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/32")), getCpuPortId());

    // +1 for ip to me
    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE), crm_ipv4_route + 1);

    auto intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_TRUE(intfs_table["lo"].ip_addresses.count(IpPrefix("192.168.1.1/24")));

    //verify delete subnet
    doIntfsOrchTask(setData, DEL_COMMAND);

    intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_TRUE(intfs_table.find("lo") == intfs_table.end());
}

TEST_F(IntfsOrchTest, addEthernetPortIPv4Subnet)
{
    uint32_t crm_ipv4_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE);

    // create Vlan1 L3 interface and add local subnet 192.168.1.1/24
    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Ethernet1:192.168.1.1/24",
              SET_COMMAND,
              {}
          } });
    doIntfsOrchTask(setData);

    // verify router interface for Vlan1 is created and is set to port orch
    EXPECT_NE(getRouterInterfaceId("Ethernet1"), SAI_NULL_OBJECT_ID);

    // verify virtual router id
    EXPECT_EQ(getVirtualRouterId("Ethernet1"), gVirtualRouterId);

    // verify subnet route
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/24")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/24")), getRouterInterfaceId("Ethernet1"));

    // verify ip to me
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/32")), getCpuPortId());

    // +2 for local subnet and ip to me
    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE), crm_ipv4_route + 2);

    //verify delete router interface
    sai_object_id_t tmp_rif_id = getRouterInterfaceId("Ethernet1");

    doIntfsOrchTask(setData, DEL_COMMAND);

    // verify router interface for Vlan1 is deleted and set to port orch
    EXPECT_EQ(getRouterInterfaceId("Ethernet1"), SAI_NULL_OBJECT_ID);

    // verify virtual router id is delete in sai
    EXPECT_EQ(getVirtualRouterId(tmp_rif_id), SAI_NULL_OBJECT_ID);
}


TEST_F(IntfsOrchTest, addVlanRouterInterfacesWithIPv4v6Subnets)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));
    EXPECT_TRUE(createVlan("Vlan2"));
    EXPECT_TRUE(addVlanMember("Vlan2", "Ethernet2"));

    uint32_t crm_ipv4_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE);
    uint32_t crm_ipv6_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV6_ROUTE);

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24", SET_COMMAND, {} },
            { "Vlan1:192.168.2.1/24", SET_COMMAND, {} },
            { "Vlan2:192.168.3.1/24", SET_COMMAND, {} },
            { "Vlan2:192.168.4.1/24", SET_COMMAND, {} },
            { "Vlan1:2012::168/64", SET_COMMAND, {} },
            { "Vlan2:2013::168/64", SET_COMMAND, {} }
          } );
    doIntfsOrchTask(setData);

    for (auto it = setData.begin(); it != setData.end(); ++it)
    {
        vector<string> keys = tokenize(kfvKey(*it), ':');
        string vlan_name = keys[0];
        IpPrefix ip_prefix(kfvKey(*it).substr(kfvKey(*it).find(':')+1));
        IpPrefix ip_addr(ip_prefix.getIp().to_string() + (ip_prefix.isV4()? "/32" : "/128"));

        EXPECT_NE(getRouterInterfaceId(vlan_name), SAI_NULL_OBJECT_ID);
        EXPECT_EQ(getVirtualRouterId(vlan_name), gVirtualRouterId);
        EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, ip_prefix), SAI_PACKET_ACTION_FORWARD);
        EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, ip_prefix), getRouterInterfaceId(vlan_name));
        EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, ip_addr), SAI_PACKET_ACTION_FORWARD);
        EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, ip_addr), getCpuPortId());
        if (ip_prefix.isV4())
        {
            MacAddress m = getNeighborDstMac(vlan_name, ip_prefix.getBroadcastIp());
            EXPECT_TRUE(m == MacAddress("ff:ff:ff:ff:ff:ff"));
            crm_ipv4_route += 2; // ip subnet route + ip2me
        }
        else
        {
            crm_ipv6_route += 2; // ipv6 subnet route + ip2me
        }
    }

    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE), crm_ipv4_route);
    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV6_ROUTE), crm_ipv6_route);

    set<IpPrefix> allRoutes = gIntfsOrch->getSubnetRoutes();
    EXPECT_EQ(allRoutes.size(), setData.size());
    for (auto it = setData.begin(); it != setData.end(); ++it)
    {
        IpPrefix ip_prefix(kfvKey(*it).substr(kfvKey(*it).find(':')+1));
        EXPECT_TRUE(allRoutes.find(ip_prefix) != allRoutes.end());
    }

    // clean up
    sai_object_id_t rif_id_v1 = getRouterInterfaceId("Vlan1");
    sai_object_id_t rif_id_v2 = getRouterInterfaceId("Vlan2");

    doIntfsOrchTask(setData, DEL_COMMAND);

    EXPECT_EQ(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getRouterInterfaceId("Vlan2"), SAI_NULL_OBJECT_ID);

    EXPECT_EQ(getVirtualRouterId(rif_id_v1), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getVirtualRouterId(rif_id_v2), SAI_NULL_OBJECT_ID);

    allRoutes = gIntfsOrch->getSubnetRoutes();
    EXPECT_EQ(allRoutes.size(), 0);
}

TEST_F(IntfsOrchTest, addVlanRouterInterfaceInVrfWithIPv4Subnet)
{
    // create VLAN 1 and add port 1 as member
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));
    EXPECT_TRUE(createVRF("VRF1"));

    uint32_t crm_ipv4_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE);

    // create Vlan1 L3 interface and add local subnet 192.168.1.1/24
    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {{"vrf_name", "VRF1"}}
          } });
    doIntfsOrchTask(setData);

    // verify router interface for Vlan1 is created and is set to port orch
    EXPECT_NE(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);

    sai_object_id_t vr_id = gVrfOrch->getVRFid("VRF1");

    // verify virtual router id
    EXPECT_EQ(getVirtualRouterId("Vlan1"), vr_id);

    // verify subnet route
    EXPECT_EQ(getRoutePacketAction(vr_id, IpPrefix("192.168.1.1/24")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(vr_id, IpPrefix("192.168.1.1/24")), getRouterInterfaceId("Vlan1"));

    // verify ip to me
    EXPECT_EQ(getRoutePacketAction(vr_id, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(vr_id, IpPrefix("192.168.1.1/32")), getCpuPortId());

    // verify subnet broadcast
    MacAddress m = getNeighborDstMac("Vlan1", IpPrefix("192.168.1.1/24").getBroadcastIp());
    EXPECT_TRUE(m == MacAddress("ff:ff:ff:ff:ff:ff"));

    // +2 for local subnet and ip to me
    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE), crm_ipv4_route + 2);

    //verify delete router interface
    sai_object_id_t tmp_rif_id = getRouterInterfaceId("Vlan1");

    doIntfsOrchTask(setData, DEL_COMMAND);

    // verify router interface for Vlan1 is deleted and set to port orch
    EXPECT_EQ(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);

    // verify virtual router id is delete in sai
    EXPECT_EQ(getVirtualRouterId(tmp_rif_id), SAI_NULL_OBJECT_ID);

    //clean up
    EXPECT_TRUE(deleteVRF("VRF1"));
}


TEST_F(IntfsOrchTest, addVlanRouterInterfaceInVnetWithIPv4Subnet)
{
    // create VLAN 1 and add port 1 as member
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));
    EXPECT_TRUE(createVnet("Vnet1", 168, "Vxlan1"));

    uint32_t crm_ipv4_route = getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE);

    // create Vlan1 L3 interface and add local subnet 192.168.1.1/24
    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {{"vnet_name", "Vnet1"}}
          } });
    doIntfsOrchTask(setData);

    // verify router interface for Vlan1 is created and is set to port orch
    EXPECT_NE(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);

    VNetOrch* vnet_orch = gDirectory.get<VNetOrch*>();
    auto *vnet_obj = vnet_orch->getTypePtr<VNetVrfObject>("Vnet1");
    sai_object_id_t vr_id = vnet_obj->getVRid();

    // verify virtual router id
    EXPECT_EQ(getVirtualRouterId("Vlan1"), vr_id);

    // verify subnet route
    EXPECT_EQ(getRoutePacketAction(vr_id, IpPrefix("192.168.1.1/24")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(vr_id, IpPrefix("192.168.1.1/24")), getRouterInterfaceId("Vlan1"));

    // verify ip to me
    EXPECT_EQ(getRoutePacketAction(vr_id, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(vr_id, IpPrefix("192.168.1.1/32")), getCpuPortId());

    // verify subnet broadcast
    MacAddress m = getNeighborDstMac("Vlan1", IpPrefix("192.168.1.1/24").getBroadcastIp());
    EXPECT_TRUE(m == MacAddress("ff:ff:ff:ff:ff:ff"));

    // +2 for local subnet and ip to me
    EXPECT_EQ(getCrmResUsedCounter(CrmResourceType::CRM_IPV4_ROUTE), crm_ipv4_route + 2);

    //verify delete router interface
    sai_object_id_t tmp_rif_id = getRouterInterfaceId("Vlan1");

    doIntfsOrchTask(setData, DEL_COMMAND);

    // verify router interface for Vlan1 is deleted and set to port orch
    EXPECT_EQ(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);

    // verify virtual router id is delete in sai
    EXPECT_EQ(getVirtualRouterId(tmp_rif_id), SAI_NULL_OBJECT_ID);
}


TEST_F(IntfsOrchTest, overlappedIPv4SubnetsOnSameRouterInterface)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {}
            },
            { "Vlan1:192.168.2.1/16",
              SET_COMMAND,
              {}
          } });
    doIntfsOrchTask(setData);

    EXPECT_NE(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getVirtualRouterId("Vlan1"), gVirtualRouterId);
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/24")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/24")), getRouterInterfaceId("Vlan1"));
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/32")), getCpuPortId());
    MacAddress m = getNeighborDstMac("Vlan1", IpPrefix("192.168.1.1/24").getBroadcastIp());
    EXPECT_TRUE(m == MacAddress("ff:ff:ff:ff:ff:ff"));
    EXPECT_FALSE(isRouteExist(gVirtualRouterId, IpPrefix("192.168.2.1/16")));

    //clean up
    sai_object_id_t tmp_rif_id = getRouterInterfaceId("Vlan1");

    doIntfsOrchTask(setData, DEL_COMMAND);

    EXPECT_EQ(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getVirtualRouterId(tmp_rif_id), SAI_NULL_OBJECT_ID);
}

TEST_F(IntfsOrchTest, sameIPv4SubnetsOnTwoRouterInterfaces)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));
    EXPECT_TRUE(createVlan("Vlan2"));
    EXPECT_TRUE(addVlanMember("Vlan2", "Ethernet2"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {}
            },
            { "Vlan2:192.168.1.1/24",
              SET_COMMAND,
              {}
          } });
    try
    {
        doIntfsOrchTask(setData);
    }
    catch (const std::exception& e)
    {
        // do nothing, add subnet route fail will raise an exception
    }

    EXPECT_NE(getRouterInterfaceId("Vlan1"), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getVirtualRouterId("Vlan1"), gVirtualRouterId);
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/24")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/24")), getRouterInterfaceId("Vlan1"));
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/32")), getCpuPortId());
    MacAddress m = getNeighborDstMac("Vlan1", IpPrefix("192.168.1.1/24").getBroadcastIp());
    EXPECT_TRUE(m == MacAddress("ff:ff:ff:ff:ff:ff"));

    EXPECT_NE(getRouterInterfaceId("Vlan2"), SAI_NULL_OBJECT_ID);
    EXPECT_EQ(getVirtualRouterId("Vlan2"), gVirtualRouterId);

    set<IpPrefix> allRoutes = gIntfsOrch->getSubnetRoutes();

    EXPECT_EQ(allRoutes.size(), 1);
    EXPECT_FALSE(allRoutes.find(IpPrefix("192.168.1.1/24")) == allRoutes.end());

    //clean up
    doIntfsOrchTask(setData, DEL_COMMAND);
}

TEST_F(IntfsOrchTest, addIpv4RouterInterfaceWithNonExistVrfName)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {{"vrf_name", "non-exist"}}
          } });
    doIntfsOrchTask(setData);

    EXPECT_TRUE(getRouterInterfaceId("Vlan1") == SAI_NULL_OBJECT_ID);
}

TEST_F(IntfsOrchTest, addIpv4RouterInterfaceWithNonExistVnetName)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {{"vnet_name", "non-exist"}}
          } });
    doIntfsOrchTask(setData);

    EXPECT_TRUE(getRouterInterfaceId("Vlan1") == SAI_NULL_OBJECT_ID);
}

TEST_F(IntfsOrchTest, getRouterIntfsId)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {}
          } });
    doIntfsOrchTask(setData);

    Port p;
    ASSERT_TRUE(gPortsOrch->getPort("Vlan1", p));

    EXPECT_EQ(gIntfsOrch->getRouterIntfsId("Vlan1"), p.m_rif_id);

    doIntfsOrchTask(setData, DEL_COMMAND);
}

TEST_F(IntfsOrchTest, setRouterInterfaceMtu)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    auto setData = deque<KeyOpFieldsValuesTuple>(
          { { "Vlan1:192.168.1.1/24",
              SET_COMMAND,
              {}
          } });
    doIntfsOrchTask(setData);

    Port p;
    ASSERT_TRUE(gPortsOrch->getPort("Vlan1", p));

    p.m_mtu = 2019;
    ASSERT_TRUE(gIntfsOrch->setRouterIntfsMtu(p));

    EXPECT_EQ(getRouterIntfsMtu("Vlan1"), 2019);

    doIntfsOrchTask(setData, DEL_COMMAND);
}

TEST_F(IntfsOrchTest, RouterIntfsRefCount)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    EXPECT_TRUE(gIntfsOrch->setIntf("Vlan1", gVirtualRouterId, nullptr));
    IntfsTable intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_EQ(intfs_table["Vlan1"].ref_count, 0);
    gIntfsOrch->increaseRouterIntfsRefCount("Vlan1");
    intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_EQ(intfs_table["Vlan1"].ref_count, 1);
    gIntfsOrch->decreaseRouterIntfsRefCount("Vlan1");
    intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_EQ(intfs_table["Vlan1"].ref_count, 0);
}

TEST_F(IntfsOrchTest, addRemoveIp2MeRoute)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    EXPECT_TRUE(gIntfsOrch->setIntf("Vlan1", gVirtualRouterId, nullptr));
    gIntfsOrch->addIp2MeRoute(gVirtualRouterId, IpPrefix("192.168.1.1/24"));
    EXPECT_EQ(getRoutePacketAction(gVirtualRouterId, IpPrefix("192.168.1.1/32")), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(getRouteNexthopId(gVirtualRouterId, IpPrefix("192.168.1.1/32")), getCpuPortId());

    gIntfsOrch->removeIp2MeRoute(gVirtualRouterId, IpPrefix("192.168.1.1/24"));
    EXPECT_FALSE(isRouteExist(gVirtualRouterId, IpPrefix("192.168.1.1/32")));
}

TEST_F(IntfsOrchTest, getSyncdIntfses)
{
    EXPECT_TRUE(createVlan("Vlan1"));
    EXPECT_TRUE(addVlanMember("Vlan1", "Ethernet1"));

    IpPrefix prefix1("192.168.1.1/24");
    IpPrefix prefix2("192.168.2.1/24");

    EXPECT_TRUE(gIntfsOrch->setIntf("Vlan1", gVirtualRouterId, &prefix1));
    EXPECT_TRUE(gIntfsOrch->setIntf("Vlan1", gVirtualRouterId, &prefix2));
    auto intfs_table = gIntfsOrch->getSyncdIntfses();
    EXPECT_TRUE(intfs_table["Vlan1"].ip_addresses.count(prefix1));
    EXPECT_TRUE(intfs_table["Vlan1"].ip_addresses.count(prefix1));
}

