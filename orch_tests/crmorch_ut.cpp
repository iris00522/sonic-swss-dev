#include <stdio.h>
#include "ut_helper.h"
#include "saiattributelist.h"
#include "saihelper.h"
#define protected public
#define private public
#include "crmorch.h"
#undef protected
#undef private


extern "C"
{
}

#ifndef _countof
#define _countof(_Ary)  (sizeof(_Ary) / sizeof(*_Ary))
#endif /* _countof */

extern void syncd_apply_view();

extern void initSaiApi();
extern sai_switch_api_t*        sai_switch_api;
extern sai_acl_api_t*          sai_acl_api;
extern sai_object_id_t            gSwitchId;
extern CrmOrch *gCrmOrch;

/* Global variables */
extern sai_object_id_t gVirtualRouterId;
extern sai_object_id_t gUnderlayIfId;
extern sai_object_id_t gSwitchId;
extern MacAddress gMacAddress;
extern MacAddress gVxlanMacAddress;

extern int gBatchSize;

extern bool gSairedisRecord;
extern bool gSwssRecord;
extern bool gLogRotate;
extern ofstream gRecordOfs;
extern string gRecordFile;

extern uint32_t set_hostif_group_attr_count;
extern uint32_t set_hostif_attr_count;
extern sai_attribute_t set_hostif_group_attr_list[20];
extern sai_attribute_t set_hostif_attr_list[20];

const map<CrmResourceType, string> crmResTypeNameMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, "IPV4_ROUTE" },
    { CrmResourceType::CRM_IPV6_ROUTE, "IPV6_ROUTE" },
    { CrmResourceType::CRM_IPV4_NEXTHOP, "IPV4_NEXTHOP" },
    { CrmResourceType::CRM_IPV6_NEXTHOP, "IPV6_NEXTHOP" },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, "IPV4_NEIGHBOR" },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, "IPV6_NEIGHBOR" },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER, "NEXTHOP_GROUP_MEMBER" },
    { CrmResourceType::CRM_NEXTHOP_GROUP, "NEXTHOP_GROUP" },
    { CrmResourceType::CRM_ACL_TABLE, "ACL_TABLE" },
    { CrmResourceType::CRM_ACL_GROUP, "ACL_GROUP" },
    { CrmResourceType::CRM_ACL_ENTRY, "ACL_ENTRY" },
    { CrmResourceType::CRM_ACL_COUNTER, "ACL_COUNTER" },
    { CrmResourceType::CRM_FDB_ENTRY, "FDB_ENTRY" }
};

const map<CrmResourceType, uint32_t> crmResSaiAvailAttrMap =
{
    { CrmResourceType::CRM_IPV4_ROUTE, SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV6_ROUTE, SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY },
    { CrmResourceType::CRM_IPV4_NEXTHOP, SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEXTHOP_ENTRY },
    { CrmResourceType::CRM_IPV6_NEXTHOP, SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEXTHOP_ENTRY },
    { CrmResourceType::CRM_IPV4_NEIGHBOR, SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_IPV6_NEIGHBOR, SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEIGHBOR_ENTRY },
    { CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER, SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_MEMBER_ENTRY },
    { CrmResourceType::CRM_NEXTHOP_GROUP, SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY },
    { CrmResourceType::CRM_ACL_TABLE, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE },
    { CrmResourceType::CRM_ACL_GROUP, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP },
    { CrmResourceType::CRM_ACL_ENTRY, SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY },
    { CrmResourceType::CRM_ACL_COUNTER, SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER },
    { CrmResourceType::CRM_FDB_ENTRY, SAI_SWITCH_ATTR_AVAILABLE_FDB_ENTRY }
};

const map<string, CrmResourceType> crmThreshTypeResMap =
{
    { "ipv4_route_threshold_type", CrmResourceType::CRM_IPV4_ROUTE },
    { "ipv6_route_threshold_type", CrmResourceType::CRM_IPV6_ROUTE },
    { "ipv4_nexthop_threshold_type", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "ipv6_nexthop_threshold_type", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "ipv4_neighbor_threshold_type", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "ipv6_neighbor_threshold_type", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "nexthop_group_member_threshold_type", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "nexthop_group_threshold_type", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "acl_table_threshold_type", CrmResourceType::CRM_ACL_TABLE },
    { "acl_group_threshold_type", CrmResourceType::CRM_ACL_GROUP },
    { "acl_entry_threshold_type", CrmResourceType::CRM_ACL_ENTRY },
    { "acl_counter_threshold_type", CrmResourceType::CRM_ACL_COUNTER },
    { "fdb_entry_threshold_type", CrmResourceType::CRM_FDB_ENTRY }
};

const map<string, CrmResourceType> crmThreshLowResMap =
{
    {"ipv4_route_low_threshold", CrmResourceType::CRM_IPV4_ROUTE },
    {"ipv6_route_low_threshold", CrmResourceType::CRM_IPV6_ROUTE },
    {"ipv4_nexthop_low_threshold", CrmResourceType::CRM_IPV4_NEXTHOP },
    {"ipv6_nexthop_low_threshold", CrmResourceType::CRM_IPV6_NEXTHOP },
    {"ipv4_neighbor_low_threshold", CrmResourceType::CRM_IPV4_NEIGHBOR },
    {"ipv6_neighbor_low_threshold", CrmResourceType::CRM_IPV6_NEIGHBOR },
    {"nexthop_group_member_low_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    {"nexthop_group_low_threshold", CrmResourceType::CRM_NEXTHOP_GROUP },
    {"acl_table_low_threshold", CrmResourceType::CRM_ACL_TABLE },
    {"acl_group_low_threshold", CrmResourceType::CRM_ACL_GROUP },
    {"acl_entry_low_threshold", CrmResourceType::CRM_ACL_ENTRY },
    {"acl_counter_low_threshold", CrmResourceType::CRM_ACL_COUNTER },
    {"fdb_entry_low_threshold", CrmResourceType::CRM_FDB_ENTRY },
};

const map<string, CrmResourceType> crmThreshHighResMap =
{
    {"ipv4_route_high_threshold", CrmResourceType::CRM_IPV4_ROUTE },
    {"ipv6_route_high_threshold", CrmResourceType::CRM_IPV6_ROUTE },
    {"ipv4_nexthop_high_threshold", CrmResourceType::CRM_IPV4_NEXTHOP },
    {"ipv6_nexthop_high_threshold", CrmResourceType::CRM_IPV6_NEXTHOP },
    {"ipv4_neighbor_high_threshold", CrmResourceType::CRM_IPV4_NEIGHBOR },
    {"ipv6_neighbor_high_threshold", CrmResourceType::CRM_IPV6_NEIGHBOR },
    {"nexthop_group_member_high_threshold", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    {"nexthop_group_high_threshold", CrmResourceType::CRM_NEXTHOP_GROUP },
    {"acl_table_high_threshold", CrmResourceType::CRM_ACL_TABLE },
    {"acl_group_high_threshold", CrmResourceType::CRM_ACL_GROUP },
    {"acl_entry_high_threshold", CrmResourceType::CRM_ACL_ENTRY },
    {"acl_counter_high_threshold", CrmResourceType::CRM_ACL_COUNTER },
    {"fdb_entry_high_threshold", CrmResourceType::CRM_FDB_ENTRY }
};

const map<string, CrmThresholdType> crmThreshTypeMap =
{
    { "percentage", CrmThresholdType::CRM_PERCENTAGE },
    { "used", CrmThresholdType::CRM_USED },
    { "free", CrmThresholdType::CRM_FREE }
};

const map<string, CrmResourceType> crmAvailCntsTableMap =
{
    { "crm_stats_ipv4_route_available", CrmResourceType::CRM_IPV4_ROUTE },
    { "crm_stats_ipv6_route_available", CrmResourceType::CRM_IPV6_ROUTE },
    { "crm_stats_ipv4_nexthop_available", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "crm_stats_ipv6_nexthop_available", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "crm_stats_ipv4_neighbor_available", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "crm_stats_ipv6_neighbor_available", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "crm_stats_nexthop_group_member_available", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "crm_stats_nexthop_group_available", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "crm_stats_acl_table_available", CrmResourceType::CRM_ACL_TABLE },
    { "crm_stats_acl_group_available", CrmResourceType::CRM_ACL_GROUP },
    { "crm_stats_acl_entry_available", CrmResourceType::CRM_ACL_ENTRY },
    { "crm_stats_acl_counter_available", CrmResourceType::CRM_ACL_COUNTER },
    { "crm_stats_fdb_entry_available", CrmResourceType::CRM_FDB_ENTRY }
};

const map<string, CrmResourceType> crmUsedCntsTableMap =
{
    { "crm_stats_ipv4_route_used", CrmResourceType::CRM_IPV4_ROUTE },
    { "crm_stats_ipv6_route_used", CrmResourceType::CRM_IPV6_ROUTE },
    { "crm_stats_ipv4_nexthop_used", CrmResourceType::CRM_IPV4_NEXTHOP },
    { "crm_stats_ipv6_nexthop_used", CrmResourceType::CRM_IPV6_NEXTHOP },
    { "crm_stats_ipv4_neighbor_used", CrmResourceType::CRM_IPV4_NEIGHBOR },
    { "crm_stats_ipv6_neighbor_used", CrmResourceType::CRM_IPV6_NEIGHBOR },
    { "crm_stats_nexthop_group_member_used", CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER },
    { "crm_stats_nexthop_group_used", CrmResourceType::CRM_NEXTHOP_GROUP },
    { "crm_stats_acl_table_used", CrmResourceType::CRM_ACL_TABLE },
    { "crm_stats_acl_group_used", CrmResourceType::CRM_ACL_GROUP },
    { "crm_stats_acl_entry_used", CrmResourceType::CRM_ACL_ENTRY },
    { "crm_stats_acl_counter_used", CrmResourceType::CRM_ACL_COUNTER },
    { "crm_stats_fdb_entry_used", CrmResourceType::CRM_FDB_ENTRY }
};

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

struct TestBase: public ::testing::Test {
    static sai_status_t sai_get_switch_attribute_(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
    {
        return that->sai_get_switch_attribute_fn(switch_id, attr_count, attr_list);
    }

    static TestBase* that;

    std::function<sai_status_t(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)>
        sai_get_switch_attribute_fn;
};

TestBase* TestBase::that = nullptr;


namespace CrmTest 
{
    using namespace std;
    using namespace testing;

    class CrmTest : public TestBase
    {
    public:
        std::shared_ptr<swss::DBConnector> m_config_db;

        CrmTest()
        {
            m_config_db = std::make_shared<swss::DBConnector>(CONFIG_DB, swss::DBConnector::DEFAULT_UNIXSOCKET, 0);
        }

        ~CrmTest()
        {
        }

        void SetUp()
        {
            sai_status_t    status;
            sai_attribute_t attr;

            static sai_service_method_table_t test_services = {
                profile_get_value,
                profile_get_next_value
            };

            status = sai_api_initialize(0, (sai_service_method_table_t*)&test_services);
            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);

            sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);
            sai_api_query(SAI_API_ACL,      (void **)&sai_acl_api);

            attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
            attr.value.booldata = true;
            ASSERT_TRUE( sai_switch_api->create_switch(&gSwitchId, 1, &attr) == SAI_STATUS_SUCCESS);

            gCrmOrch = new CrmOrch(m_config_db.get(), CFG_CRM_TABLE_NAME);
        }

        void TearDown()
        {
            sai_status_t    status;

            status = sai_switch_api->remove_switch(gSwitchId);
            ASSERT_TRUE(status == SAI_STATUS_SUCCESS);

            gSwitchId = 0;
            sai_api_uninitialize();
            delete gCrmOrch;
            sai_switch_api = nullptr;
            sai_acl_api = nullptr;
        }
    };

    TEST_F(CrmTest, CRM_MAP)
    {
        ASSERT_TRUE(gCrmOrch != NULL);
        ASSERT_TRUE(gCrmOrch->m_countersDb != NULL);
        ASSERT_TRUE(gCrmOrch->m_countersCrmTable != NULL);
        ASSERT_TRUE(gCrmOrch->m_timer != NULL);
        //check default value
        for (const auto &res : crmResTypeNameMap){
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).name,res.second);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).thresholdType,CrmThresholdType::CRM_PERCENTAGE);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).lowThreshold,70);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).highThreshold,85);
        }
        ASSERT_EQ(gCrmOrch->m_pollingInterval.count(),(5 * 60));
    }

    TEST_F(CrmTest, ResUsedCounter)
    {

        //test case list :  IPV4_ROUTE IPV6_ROUTE IPV4_NEXTHOP IPV6_NEXTHOP 
        //                  NEXTHOP_GROUP_MEMBER NEXTHOP_GROUP FDB_ENTRY
        int used_num = 5;

        for (auto &i : crmResTypeNameMap){
            if(i.first == CrmResourceType::CRM_ACL_TABLE || i.first == CrmResourceType::CRM_ACL_GROUP
               || i.first == CrmResourceType::CRM_ACL_ENTRY || i.first == CrmResourceType::CRM_ACL_COUNTER)
            continue;
            gCrmOrch->m_resourcesMap.at(i.first).countersMap["STATS"].usedCounter = used_num;
            gCrmOrch->incCrmResUsedCounter(i.first);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).countersMap["STATS"].usedCounter,(used_num+1));
            gCrmOrch->decCrmResUsedCounter(i.first);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).countersMap["STATS"].usedCounter,used_num);
        }
    }


    TEST_F(CrmTest, AclUsedCounter)
    {
        CrmResourceType rnage[] = {CrmResourceType::CRM_ACL_TABLE,CrmResourceType::CRM_ACL_GROUP};
        int used_num = 0;
        sai_object_id_t table_oid = 10, table_oid1 = 20;
        string str,str1;

        //test case list :  ACL_TABLE ACL_GROUP

        for(auto &k :rnage ){
            used_num = k == CrmResourceType::CRM_ACL_TABLE ? 0 : 6;
            for (int i = SAI_ACL_STAGE_INGRESS;i<= SAI_ACL_STAGE_EGRESS;i++){
                for (int j= SAI_ACL_BIND_POINT_TYPE_PORT;j<=SAI_ACL_BIND_POINT_TYPE_SWITCH;j++){
                    int l = j == SAI_ACL_BIND_POINT_TYPE_SWITCH ? SAI_ACL_BIND_POINT_TYPE_PORT :j+1;
                    //table 1
                    str = gCrmOrch->getCrmAclKey((sai_acl_stage_t)i, (sai_acl_bind_point_type_t)j);
                    gCrmOrch->m_resourcesMap.at(k).countersMap[str].usedCounter = used_num;
                    gCrmOrch->m_resourcesMap.at(k).countersMap[str].id = table_oid;
                    gCrmOrch->incCrmAclUsedCounter(k, (sai_acl_stage_t)i, (sai_acl_bind_point_type_t)j);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(k).countersMap[str].usedCounter,(used_num+1));
                    if(k == CrmResourceType::CRM_ACL_TABLE){
                         ASSERT_TRUE(gCrmOrch->m_resourcesMap.at(k).countersMap.find(str) != gCrmOrch->m_resourcesMap.at(k).countersMap.end());
                    }

                    //table 2
                    str1 = gCrmOrch->getCrmAclKey((sai_acl_stage_t)i, (sai_acl_bind_point_type_t)l);
                    gCrmOrch->m_resourcesMap.at(k).countersMap[str1].usedCounter = used_num;
                    gCrmOrch->m_resourcesMap.at(k).countersMap[str1].id = table_oid1;
                    gCrmOrch->incCrmAclUsedCounter(k, (sai_acl_stage_t)i, (sai_acl_bind_point_type_t)l);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(k).countersMap[str1].usedCounter,(used_num+1));
                    if(k == CrmResourceType::CRM_ACL_TABLE){
                         ASSERT_TRUE(gCrmOrch->m_resourcesMap.at(k).countersMap.find(str1) != gCrmOrch->m_resourcesMap.at(k).countersMap.end());
                    }

                    // delete table 2 first
                    gCrmOrch->decCrmAclUsedCounter(k, (sai_acl_stage_t)i, (sai_acl_bind_point_type_t)l, table_oid1);
                    gCrmOrch->decCrmAclUsedCounter(k, (sai_acl_stage_t)i, (sai_acl_bind_point_type_t)j, table_oid);
                    //check erase countersMap and not need to check usedCounter
                    if(k == CrmResourceType::CRM_ACL_TABLE){
                        ASSERT_TRUE(gCrmOrch->m_resourcesMap.at(k).countersMap.find(str1) == gCrmOrch->m_resourcesMap.at(k).countersMap.end());
                        ASSERT_TRUE(gCrmOrch->m_resourcesMap.at(k).countersMap.find(str) == gCrmOrch->m_resourcesMap.at(k).countersMap.end());
                    }else
                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(k).countersMap[str].usedCounter,used_num);
                }

            }
            // out of the sai_acl_stage_t (ingress or egress)
            gCrmOrch->m_resourcesMap.at(k).countersMap["ACL_STATS:EGRESS:PORT"].usedCounter = used_num;
            gCrmOrch->incCrmAclUsedCounter(k,(sai_acl_stage_t)3, SAI_ACL_BIND_POINT_TYPE_PORT);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(k).countersMap["ACL_STATS:EGRESS:PORT"].usedCounter,used_num);

            // out of the sai_acl_bind_point_type_t
            gCrmOrch->m_resourcesMap.at(k).countersMap["ACL_STATS:EGRESS:PORT"].usedCounter = used_num;
            gCrmOrch->incCrmAclUsedCounter(k,SAI_ACL_STAGE_INGRESS, (_sai_acl_bind_point_type_t)12);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(k).countersMap["ACL_STATS:EGRESS:PORT"].usedCounter,used_num);
        }
    }

    TEST_F(CrmTest, AclTableUsedCounter)
    {
        sai_object_id_t tableId  = 10;
        CrmResourceType rnage[] = {CrmResourceType::CRM_ACL_ENTRY,CrmResourceType::CRM_ACL_COUNTER};
        int used_num = 15;
        string str = gCrmOrch->getCrmAclTableKey(tableId);
        //test case list :  ACL_ENTRY ACL_COUNTER

        for (auto &i : rnage){
            gCrmOrch->m_resourcesMap.at(i).countersMap[str].usedCounter = used_num;
            gCrmOrch->incCrmAclTableUsedCounter(i, tableId);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i).countersMap[str].usedCounter,(used_num+1));
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i).countersMap[str].id,tableId);
            gCrmOrch->decCrmAclTableUsedCounter(i, tableId);
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i).countersMap[str].usedCounter,used_num);
        }
    }

    TEST_F(CrmTest, Crm_Config_PollingInterval)
    {
        uint32_t interval = 20;
        auto consumer = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), std::string(CFG_CRM_TABLE_NAME), 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));

        //check default
        ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, (5 * 60));
        {
            //check PollingInterval
            std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ "polling_interval", to_string(interval) }}} };
            consumer->addToSync(setData);
            gCrmOrch->doTask(*consumer);
            ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, interval);
        }
    }

    TEST_F(CrmTest, Crm_Config_thresholdType)
    {
        sai_object_id_t tableId  = 10;
        auto consumer = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), std::string(CFG_CRM_TABLE_NAME), 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));

        for (auto &i : crmThreshTypeResMap){
            //check default
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).thresholdType,CrmThresholdType::CRM_PERCENTAGE);

            for (auto &j : crmThreshTypeMap){
                {
                    //check threshold type
                    std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ i.first, j.first }}} };
                    consumer->addToSync(setData);
                    gCrmOrch->doTask(*consumer);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).thresholdType,j.second);
                }
            }
        }
    }

    TEST_F(CrmTest, Crm_Config_thresholdlow)
    {
        sai_object_id_t tableId  = 10;
        uint32_t lowThreshold = 60;
        auto consumer = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), std::string(CFG_CRM_TABLE_NAME), 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));

        for (const auto &i : crmThreshLowResMap){
            //check default
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).lowThreshold,70);
            {
                //check low threshold
                std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ i.first, to_string(lowThreshold) }}} };
                consumer->addToSync(setData);
                gCrmOrch->doTask(*consumer);
                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).lowThreshold,lowThreshold);
            }
        }
    }

    TEST_F(CrmTest, Crm_Config_thresholdhigh)
    {
        sai_object_id_t tableId  = 10;
        uint32_t highThreshold = 90;
        auto consumer = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), std::string(CFG_CRM_TABLE_NAME), 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));

        for (const auto &i : crmThreshHighResMap){
            //check default
            ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).highThreshold,85);
            {
                //check low threshold
                std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ i.first, to_string(highThreshold) }}} };
                consumer->addToSync(setData);
                gCrmOrch->doTask(*consumer);
                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.second).highThreshold,highThreshold);
            }
        }
    }

    TEST_F(CrmTest, Crm_Config_UnexpectedCase)
    {
        uint32_t interval = 20, interval1 = 30;
        auto consumer = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), std::string(CFG_CRM_TABLE_NAME), 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));
        auto consumer_test = std::unique_ptr<Consumer>(new Consumer(new swss::ConsumerStateTable(m_config_db.get(), "Test", 1, 1), gCrmOrch, std::string(CFG_CRM_TABLE_NAME)));

        //check default
        ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, (5 * 60));
        {
            //check error table name
            std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ "polling_interval", to_string(interval) }}} };
            consumer_test->addToSync(setData);
            gCrmOrch->doTask(*consumer_test);
            ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, interval );
        }
        {
            //check error ATTR
            std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "DEL", {{ "polling_interval", to_string(interval1) }}} };
            consumer->addToSync(setData);
            std::deque<KeyOpFieldsValuesTuple> setData1 = { {"CRM", "SET", {{ "ipv4_route_high_threshold", to_string(100) }}} };
            consumer->addToSync(setData);
            gCrmOrch->doTask(*consumer);
            ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, interval);
        }
        {
            // COMMAND unexpected
            std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "ADD", {{ "polling_interval", to_string(interval1) }}} };
            consumer->addToSync(setData);
            gCrmOrch->doTask(*consumer);
            ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, interval);
        }

        {
            // field unexpected
            std::deque<KeyOpFieldsValuesTuple> setData = { {"CRM", "SET", {{ "polling_interval1", to_string(interval1) }}} };
            consumer->addToSync(setData);
            gCrmOrch->doTask(*consumer);
            ASSERT_EQ(gCrmOrch->m_timer->m_interval.it_interval.tv_sec, interval);
        }
    }

    
    TEST_F(CrmTest, CRM_getUsedCounters)
    {
        sai_attribute_t attr;
        sai_status_t    status;
        vector<sai_attribute_t> table_attrs;
        SelectableTimer *timer = new SelectableTimer(timespec { .tv_sec = 300, .tv_nsec = 0 });
        CrmResourceType type;

        for (auto &res : gCrmOrch->m_resourcesMap)
        {
            switch (res.first)
            {
                case CrmResourceType::CRM_IPV4_ROUTE:
                case CrmResourceType::CRM_IPV6_ROUTE:
                case CrmResourceType::CRM_IPV4_NEXTHOP:
                case CrmResourceType::CRM_IPV6_NEXTHOP:
                case CrmResourceType::CRM_IPV4_NEIGHBOR:
                case CrmResourceType::CRM_IPV6_NEIGHBOR:
                case CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER:
                case CrmResourceType::CRM_NEXTHOP_GROUP:
                case CrmResourceType::CRM_FDB_ENTRY:
                {
                    int used_num = 42;
                    vector<FieldValueTuple> fvTuples;
                    
                    //check COUNTERS_DB attr sai
                    gCrmOrch->m_resourcesMap.at(res.first).countersMap["STATS"].usedCounter = used_num;
                    gCrmOrch->doTask(*timer);
                    gCrmOrch->m_countersCrmTable->get("STATS", fvTuples);
                    for (const auto &tbl : crmUsedCntsTableMap){
                        if(tbl.second != res.first) continue;
                        for (const auto& fv : fvTuples){
                            if(tbl.first != fvField(fv)) continue;
                            ASSERT_EQ(fvValue(fv),to_string(used_num));
                        }
                    }      
                    break;
                }
        
                case CrmResourceType::CRM_ACL_TABLE:
                case CrmResourceType::CRM_ACL_GROUP:
                {
                    int used_num = 42;
                    vector<FieldValueTuple> fvTuples;
                    
                    //check COUNTERS_DB attr sai
                    for (int i = SAI_ACL_STAGE_INGRESS;i<=SAI_ACL_STAGE_EGRESS;i++){
                        for (int j= SAI_ACL_BIND_POINT_TYPE_PORT;j<=SAI_ACL_BIND_POINT_TYPE_SWITCH;j++){
                            gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclKey((sai_acl_stage_t)i, (sai_acl_bind_point_type_t)j)].usedCounter = used_num;
                            gCrmOrch->doTask(*timer);
                            gCrmOrch->m_countersCrmTable->get(gCrmOrch->getCrmAclKey((sai_acl_stage_t)i, (sai_acl_bind_point_type_t)j), fvTuples);
                            for (const auto &tbl : crmUsedCntsTableMap){
                                if(tbl.second != res.first) continue;
                                for (const auto& fv : fvTuples){
                                    if(tbl.first != fvField(fv)) continue;
                                    ASSERT_EQ(fvValue(fv),to_string(used_num));
                                }
                            }
                        }
                    }
                    break;
                }

                case CrmResourceType::CRM_ACL_ENTRY:
                case CrmResourceType::CRM_ACL_COUNTER:
                {
                    sai_object_id_t tableId  = 10;
                    int used_num = 42;
                    vector<FieldValueTuple> fvTuples;

                    //check COUNTERS_DB attr sai
                    gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclTableKey(tableId)].usedCounter = used_num;
                    gCrmOrch->doTask(*timer);
                    gCrmOrch->m_countersCrmTable->get(gCrmOrch->getCrmAclTableKey(tableId), fvTuples);
                    for (const auto &tbl : crmUsedCntsTableMap){
                        if(tbl.second != res.first) continue;
                        for (const auto& fv : fvTuples){
                            if(tbl.first != fvField(fv)) continue;
                            ASSERT_EQ(fvValue(fv),to_string(used_num));
                        }
                    }
                    break;
                }
            }
        }
    }
    
    TEST_F(CrmTest, CRM_getAvailableCounters)
    {
        //SWSS_LOG_ENTER();
        sai_attribute_t attr,attr1;
        sai_status_t    status;
        vector<sai_attribute_t> table_attrs;
        SelectableTimer *timer = new SelectableTimer(timespec { .tv_sec = 300, .tv_nsec = 0 });

        for (auto &res : gCrmOrch->m_resourcesMap)
        {
            attr.id = crmResSaiAvailAttrMap.at(res.first);
            switch (attr.id)
            {
                case SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEXTHOP_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEXTHOP_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEIGHBOR_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEIGHBOR_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_MEMBER_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY:
                case SAI_SWITCH_ATTR_AVAILABLE_FDB_ENTRY:
                {
                    vector<FieldValueTuple> fvTuples;
                    int avail_num = 42;

                    // allow set on readonly attribute
                    meta_unittests_enable(true);
                    ASSERT_TRUE(meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_SWITCH, attr.id)== SAI_STATUS_SUCCESS);
                    attr.value.u32 = avail_num;
                    ASSERT_TRUE(sai_switch_api->set_switch_attribute(gSwitchId, &attr) == SAI_STATUS_SUCCESS);
                    gCrmOrch->doTask(*timer);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap["STATS"].availableCounter,avail_num);
                     //check COUNTERS_DB attr sai
                    gCrmOrch->m_countersCrmTable->get("STATS", fvTuples);
                    for (const auto &tbl : crmAvailCntsTableMap){
                        if(tbl.second != res.first) continue;
                        for (const auto& fv : fvTuples){
                            if(tbl.first != fvField(fv)) continue;
                            ASSERT_EQ(fvValue(fv),to_string(avail_num));
                        }
                    }
                    break;
                }

                case SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE:
                case SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP:
                {
                    vector<sai_acl_resource_t> resources(2);
                    vector<FieldValueTuple> fvTuples,fvTuples1;
                    int avail_num = 80, avail_num1 = 70;

                    attr.value.aclresource.count = 2;
                    attr.value.aclresource.list = resources.data();
                    attr.value.aclresource.list[0].stage = SAI_ACL_STAGE_INGRESS;
                    attr.value.aclresource.list[0].bind_point = SAI_ACL_BIND_POINT_TYPE_LAG;
                    attr.value.aclresource.list[0].avail_num = avail_num;
                    attr.value.aclresource.list[1].stage = SAI_ACL_STAGE_EGRESS;
                    attr.value.aclresource.list[1].bind_point = SAI_ACL_BIND_POINT_TYPE_VLAN;
                    attr.value.aclresource.list[1].avail_num = avail_num1;
                    // allow set on readonly attribute
                    meta_unittests_enable(true);
                    ASSERT_TRUE(meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_SWITCH, attr.id)== SAI_STATUS_SUCCESS);
                    ASSERT_TRUE(sai_switch_api->set_switch_attribute(gSwitchId, &attr) == SAI_STATUS_SUCCESS);
                    gCrmOrch->doTask(*timer);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_INGRESS,SAI_ACL_BIND_POINT_TYPE_LAG)].availableCounter,avail_num);
                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_EGRESS,SAI_ACL_BIND_POINT_TYPE_VLAN)].availableCounter,avail_num1);

                    //check COUNTERS_DB attr sai
                    gCrmOrch->m_countersCrmTable->get(gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_INGRESS,SAI_ACL_BIND_POINT_TYPE_LAG), fvTuples);
                    gCrmOrch->m_countersCrmTable->get(gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_EGRESS,SAI_ACL_BIND_POINT_TYPE_VLAN), fvTuples1);
                    for (const auto &tbl : crmAvailCntsTableMap){
                        if(tbl.second != res.first) continue;
                        for (const auto& fv : fvTuples){
                            if(tbl.first != fvField(fv)) continue;
                            ASSERT_EQ(fvValue(fv),to_string(avail_num));
                        }
                        for (const auto& fv : fvTuples1){
                            if(tbl.first != fvField(fv)) continue;
                            ASSERT_EQ(fvValue(fv),to_string(avail_num1));
                        }
                    }

                    //if the count is over CRM_ACL_RESOURCE_COUNT, it also can parser success
                    {
                        int count = 300;
                        vector<sai_acl_resource_t> resources(count);
                        vector<FieldValueTuple> fvTuples;
                        int avail_num = 1000;

                        attr.value.aclresource.count = count;
                        attr.value.aclresource.list = resources.data();
                        for(int cnt=0;cnt<count;cnt++ ){
                            attr.value.aclresource.list[cnt].stage = SAI_ACL_STAGE_INGRESS;
                            attr.value.aclresource.list[cnt].bind_point = SAI_ACL_BIND_POINT_TYPE_LAG;
                            attr.value.aclresource.list[cnt].avail_num = avail_num - (cnt+1);
                        }
                        // allow set on readonly attribute
                        meta_unittests_enable(true);
                        ASSERT_TRUE(meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_SWITCH, attr.id)== SAI_STATUS_SUCCESS);
                        ASSERT_TRUE(sai_switch_api->set_switch_attribute(gSwitchId, &attr) == SAI_STATUS_SUCCESS);
                        gCrmOrch->doTask(*timer);
                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_INGRESS,SAI_ACL_BIND_POINT_TYPE_LAG)].availableCounter,avail_num-count);
                        //check COUNTERS_DB attr sai
                        gCrmOrch->m_countersCrmTable->get(gCrmOrch->getCrmAclKey(SAI_ACL_STAGE_INGRESS,SAI_ACL_BIND_POINT_TYPE_LAG), fvTuples);
                        for (const auto &tbl : crmAvailCntsTableMap){
                            if(tbl.second != res.first) continue;
                            for (const auto& fv : fvTuples){
                                if(tbl.first != fvField(fv)) continue;
                                ASSERT_EQ(fvValue(fv),to_string(avail_num-count));
                            }
                        }
                    }
                    break;
                }

                case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY:
                case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER:
                {
                    sai_object_id_t tableId = 0, tableId1 = 0;

                    table_attrs.clear();
                    attr1.id = SAI_ACL_TABLE_ATTR_ACL_STAGE;
                    attr1.value.s32 = SAI_ACL_STAGE_INGRESS ;
                    table_attrs.push_back(attr1);

                    //only create table and set table id into entry/counter table.
                    status = sai_acl_api->create_acl_table(&tableId, gSwitchId, (uint32_t)table_attrs.size(), table_attrs.data());
                    ASSERT_EQ(status,SAI_STATUS_SUCCESS);
                    gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclTableKey(tableId)].id = tableId;

                    // allow set on readonly attribute
                    meta_unittests_enable(true);
                    ASSERT_TRUE(meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_ACL_TABLE, attr.id)== SAI_STATUS_SUCCESS);
                    attr.value.u32 = 42;
                    ASSERT_TRUE(sai_acl_api->set_acl_table_attribute(tableId, &attr) == SAI_STATUS_SUCCESS);
                    gCrmOrch->doTask(*timer);

                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclTableKey(tableId)].availableCounter,42);

                    status = sai_acl_api->create_acl_table(&tableId1, gSwitchId, (uint32_t)table_attrs.size(), table_attrs.data());
                    ASSERT_EQ(status,SAI_STATUS_SUCCESS);
                    gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclTableKey(tableId1)].id = tableId1;

                    ASSERT_TRUE(meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_ACL_TABLE, attr.id)== SAI_STATUS_SUCCESS);
                    attr.value.u32 = 50;
                    ASSERT_TRUE(sai_acl_api->set_acl_table_attribute(tableId1, &attr) == SAI_STATUS_SUCCESS);
                    gCrmOrch->doTask(*timer);

                    ASSERT_EQ(gCrmOrch->m_resourcesMap.at(res.first).countersMap[gCrmOrch->getCrmAclTableKey(tableId1)].availableCounter,50);
                    ASSERT_TRUE(gCrmOrch->m_resourcesMap.at(res.first).countersMap.find("ACL_TABLE_STATS:0x8") == gCrmOrch->m_resourcesMap.at(res.first).countersMap.end());// can't find
                    break;
                }
            }
        }
    }

    TEST_F(CrmTest, CRM_checkCrmThresholds)
    {
    
        SelectableTimer *timer = new SelectableTimer(timespec { .tv_sec = 300, .tv_nsec = 0 });
        int highThreshold = 90, lowThreshold = 60;
        int loop_times = 5;
        for (auto &i : gCrmOrch->m_resourcesMap){
            gCrmOrch->m_resourcesMap.at(i.first).highThreshold = highThreshold;
            gCrmOrch->m_resourcesMap.at(i.first).lowThreshold =  lowThreshold;
            switch (i.first)
            {
                case CrmResourceType::CRM_IPV4_ROUTE:
                case CrmResourceType::CRM_IPV6_ROUTE:
                case CrmResourceType::CRM_IPV4_NEXTHOP:
                case CrmResourceType::CRM_IPV6_NEXTHOP:
                case CrmResourceType::CRM_IPV4_NEIGHBOR:
                case CrmResourceType::CRM_IPV6_NEIGHBOR:
                case CrmResourceType::CRM_NEXTHOP_GROUP_MEMBER:
                case CrmResourceType::CRM_NEXTHOP_GROUP:
                case CrmResourceType::CRM_FDB_ENTRY:
                case CrmResourceType::CRM_ACL_ENTRY:
                case CrmResourceType::CRM_ACL_COUNTER:
                {
                    sai_object_id_t tableId  = 10;
                    string str;
                    if(i.first == CrmResourceType::CRM_ACL_ENTRY || i.first == CrmResourceType::CRM_ACL_COUNTER )
                        str = gCrmOrch->getCrmAclTableKey(tableId);
                    else
                        str = "STATS";

                     for(auto &j : crmThreshTypeMap){
                        gCrmOrch->m_resourcesMap.at(i.first).thresholdType = j.second;
                        switch(j.second)
                        {
                            case CrmThresholdType::CRM_PERCENTAGE:
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 5;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 95;
                                for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 30;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 70;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 50;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 50;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                break;
                             case CrmThresholdType::CRM_USED:
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 0;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 100;
                                for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 0;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 70;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 0;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 50;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                break;
                             case CrmThresholdType::CRM_FREE:  
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 100;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 70;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 0;
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                gCrmOrch->doTask(*timer);
                                ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                break;
                        }
                    } 
                    break;
                }

                case CrmResourceType::CRM_ACL_TABLE:
                case CrmResourceType::CRM_ACL_GROUP:
                {
                    string str;
                    for(auto &j : crmThreshTypeMap){
                        gCrmOrch->m_resourcesMap.at(i.first).thresholdType = j.second;
                        for (int m = SAI_ACL_STAGE_INGRESS;m<= SAI_ACL_STAGE_EGRESS;m++){
                            for (int l= SAI_ACL_BIND_POINT_TYPE_PORT;l<=SAI_ACL_BIND_POINT_TYPE_SWITCH;l++){
                                str = gCrmOrch->getCrmAclKey((sai_acl_stage_t)m, (sai_acl_bind_point_type_t)l);
                                switch(j.second)
                                {
                                    case CrmThresholdType::CRM_PERCENTAGE:
                                    {
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 5;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 95;
                                        for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 30;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 70;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 50;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 50;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                    }
                                    break;
                                    case CrmThresholdType::CRM_USED:
                                    {
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 100;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 100;
                                        for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 100;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 70;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 100;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 50;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                        break;
                                    }
                                    case CrmThresholdType::CRM_FREE:
                                    {
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 100;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                        for(int loop = 0;loop<loop_times;loop ++) gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 70;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,loop_times);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].availableCounter = 0;
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap[str].usedCounter = 0;
                                        gCrmOrch->doTask(*timer);
                                        ASSERT_EQ(gCrmOrch->m_resourcesMap.at(i.first).exceededLogCounter,0);
                                        gCrmOrch->m_resourcesMap.at(i.first).countersMap.erase(str);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }

        }
    }

}
