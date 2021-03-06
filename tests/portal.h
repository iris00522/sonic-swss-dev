#pragma once

#include "orch.h"

#define private public
#define protected public

#include "aclorch.h"
#include "crmorch.h"
#include "copporch.h"

#undef protected
#undef private

struct Portal {
    struct AclRuleInternal {
        static sai_object_id_t getRuleOid(const AclRule* aclRule)
        {
            return aclRule->m_ruleOid;
        }

        static const map<sai_acl_entry_attr_t, sai_attribute_value_t>& getMatches(const AclRule* aclRule)
        {
            return aclRule->m_matches;
        }

        static const map<sai_acl_entry_attr_t, sai_attribute_value_t>& getActions(const AclRule* aclRule)
        {
            return aclRule->m_actions;
        }
    };

    struct AclOrchInternal {
        static const map<sai_object_id_t, AclTable>& getAclTables(const AclOrch* aclOrch)
        {
            return aclOrch->m_AclTables;
        }
    };

    struct CrmOrchInternal {
        static const std::map<CrmResourceType, CrmOrch::CrmResourceEntry>& getResourceMap(const CrmOrch* crmOrch)
        {
            return crmOrch->m_resourcesMap;
        }

        static std::string getCrmAclKey(CrmOrch* crmOrch, sai_acl_stage_t stage, sai_acl_bind_point_type_t bindPoint)
        {
            return crmOrch->getCrmAclKey(stage, bindPoint);
        }

        static std::string getCrmAclTableKey(CrmOrch* crmOrch, sai_object_id_t id)
        {
            return crmOrch->getCrmAclTableKey(id);
        }

        static void getResAvailableCounters(CrmOrch* crmOrch)
        {
            crmOrch->getResAvailableCounters();
        }
    };

    struct CoppOrchInternal {
        static const object_map& getTrapGroupMap(const CoppOrch* coppOrch)
        {
            return coppOrch->m_trap_group_map;
        }

        static const TrapIdTrapGroupTable& getTrapIdTrapGroupMap(const CoppOrch* coppOrch)
        {
            return coppOrch->m_syncdTrapIds;
        }

        static const TrapGroupPolicerTable& getTrapGroupPolicerMap(const CoppOrch* coppOrch)
        {
            return coppOrch->m_trap_group_policer_map;
        }
    };
};
