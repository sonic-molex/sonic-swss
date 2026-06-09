#include "ocmorch.h"
#include "schema.h"
#include <saiexperimentalotnocm.h>
#include <cstdlib>


extern sai_otn_ocm_api_t *sai_otn_ocm_api;

#define OTN_OCM_NOTIFICATION                         "OTN_OCM_NOTIFICATION"
#define OTN_OCM_REPLY                                "OTN_OCM_REPLY"

#define OTN_OCM_CHANNEL_NOTIFICATION                 "OTN_OCM_CHANNEL_NOTIFICATION"
#define OTN_OCM_CHANNEL_REPLY                        "OTN_OCM_CHANNEL_REPLY"
#define OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP           "OTN_OCM_CHANNEL_FLEX_COUNTER"
#define OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS  1000 // ms
#define OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE        true

OcmOrch::OcmOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OCM, CounterType::OTN_OCM_ATTR)
{
    SWSS_LOG_ENTER();

    // For OCM channel pluggin
    std::string scriptPath = "otn_ocm_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_OCM_CHANNEL_PLUGIN_FIELD,
                      OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OCM_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OCM_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OCM_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OCM_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OCM_REPLY);

    m_createFunc = sai_otn_ocm_api->create_otn_ocm;
    m_removeFunc = sai_otn_ocm_api->remove_otn_ocm;
    m_setFunc = sai_otn_ocm_api->set_otn_ocm_attribute;
    m_getFunc = sai_otn_ocm_api->get_otn_ocm_attribute;

}

// OCM channel
OcmChannelOrch::OcmChannelOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OCM_CHANNEL, CounterType::OTN_OCM_CHANNEL_ATTR)
{
    SWSS_LOG_ENTER();

    // For OCM channel flex counter
    createFlexCounter("",
                      "",
                      OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OCM_CHANNEL_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OCM_CHANNEL_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OCM_CHANNEL_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OCM_CHANNEL_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OCM_CHANNEL_REPLY);

    m_createFunc = sai_otn_ocm_api->create_otn_ocm_channel;
    m_removeFunc = sai_otn_ocm_api->remove_otn_ocm_channel;
    m_setFunc = sai_otn_ocm_api->set_otn_ocm_channel_attribute;
    m_getFunc = sai_otn_ocm_api->get_otn_ocm_channel_attribute;

}

void OcmChannelOrch::addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs)
{
    // Expected key format: "<monitor-name>|<lower-frequency>|<upper-frequency>"
    auto sep1 = key.find('|');
    auto sep2 = (sep1 == std::string::npos) ? std::string::npos : key.find('|', sep1 + 1);
    if (sep1 == std::string::npos || sep2 == std::string::npos)
    {
        SWSS_LOG_WARN("Invalid OCM channel key format: %s", key.c_str());
        return;
    }

    // SAI_OTN_OCM_CHANNEL_ATTR_NAME (mandatory): media channel name
    sai_attribute_t nameAttr;
    nameAttr.id = SAI_OTN_OCM_CHANNEL_ATTR_NAME;
    nameAttr.value.u8list.count = static_cast<uint32_t>(key.size());
    nameAttr.value.u8list.list = reinterpret_cast<uint8_t*>(const_cast<char*>(key.data()));
    attrs.insert(attrs.begin(), nameAttr);

    std::string lowerStr = key.substr(sep1 + 1, sep2 - sep1 - 1);
    std::string upperStr = key.substr(sep2 + 1);
    uint64_t lowerFreq = std::strtoull(lowerStr.c_str(), nullptr, 10);
    uint64_t upperFreq = std::strtoull(upperStr.c_str(), nullptr, 10);

    sai_attribute_t lowerAttr;
    lowerAttr.id = SAI_OTN_OCM_CHANNEL_ATTR_LOWER_FREQUENCY;
    lowerAttr.value.u64 = lowerFreq;
    attrs.insert(attrs.begin(), lowerAttr);

    sai_attribute_t upperAttr;
    upperAttr.id = SAI_OTN_OCM_CHANNEL_ATTR_UPPER_FREQUENCY;
    upperAttr.value.u64 = upperFreq;
    attrs.insert(attrs.begin(), upperAttr);
}
