#include "otndeviceorch.h"
#include "schema.h"
#include "sai_serialize.h"
#include "sai_serialize_otn.h"
#include "dbconnector.h"
#include "table.h"

#include <ctime>
#include <cstdio>
#include <unistd.h>

extern sai_otn_device_api_t*            sai_otn_device_api;
extern sai_object_id_t                  gSwitchId;
extern sai_switch_api_t*                sai_switch_api;


#define OTN_DEVICE_NOTIFICATION                         "OTN_DEVICE_NOTIFICATION"
#define OTN_DEVICE_REPLY                                "OTN_DEVICE_REPLY"

static const char* otnAlarmSeverityStr(sai_otn_alarm_severity_t sev)
{
    switch (sev)
    {
        case SAI_OTN_ALARM_SEVERITY_CRITICAL: return "CRITICAL";
        case SAI_OTN_ALARM_SEVERITY_MAJOR:    return "MAJOR";
        case SAI_OTN_ALARM_SEVERITY_MINOR:    return "MINOR";
        case SAI_OTN_ALARM_SEVERITY_INFO:     return "INFORMATIONAL";
        default:                              return "INFORMATIONAL";
    }
}

// static const char* otnAlarmActionStr(sai_otn_alarm_action_t action)
// {
//     switch (action)
//     {
//         case SAI_OTN_ALARM_ACTION_RAISE: return "RAISE";
//         case SAI_OTN_ALARM_ACTION_CLEAR: return "CLEAR";
//         default:                         return "RAISE";
//     }
// }

/** Format one OTN alarm as syslog line using eventd-compatible severity and action strings.
 *  Format: <ISO8601>, <SEVERITY>, <nodename>:<entity>, <event_name>, <description>[, <ACTION>]
 *  ACTION is omitted when not raise or clear. */
static std::string formatOtnAlarmEventSyslog(const sai_otn_alarm_event_data_t* data, const char* nodename,
                                            const std::string& entity_display)
{
    SWSS_LOG_ENTER();

    if (data == nullptr)
        return "";

    std::string event_name_str;
    std::string desc_str;
    std::time_t sec = static_cast<std::time_t>(data->timestamp.tv_sec);
    std::tm tm_buf{};
    gmtime_r(&sec, &tm_buf);
    char iso_ts[32];
    strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    const char* node = (nodename && *nodename) ? nodename : "switch";
    std::string node_entity = std::string(node) + ":" + entity_display;

    if (data->event_name.list && data->event_name.count > 0)
        event_name_str.assign((char*)data->event_name.list, data->event_name.count);
    else
    {
        SWSS_LOG_ERROR("OTN alarm event_name is missing, skipping syslog");
        return "";
    }

    if (data->description.list && data->description.count > 0)
        desc_str.assign((char*)data->description.list, data->description.count);
    else
        desc_str = "Unknown";

    std::string result = std::string(iso_ts) + ", " + otnAlarmSeverityStr(data->severity) + ", " +
                         node_entity + ", " + event_name_str + ", " + desc_str;

    // const char* action = otnAlarmActionStr(data->action);
    // if (action)
    //     result += std::string(", ") + action;

    return result;
}

OtnDeviceOrch::OtnDeviceOrch(DBConnector *db, const std::vector<std::string> &table_names)
    : ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_DEVICE)
{
    SWSS_LOG_ENTER();

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_DEVICE_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_DEVICE_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_DEVICE_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_DEVICE_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_DEVICE_REPLY);

    m_createFunc = sai_otn_device_api->create_otn_device;
    m_removeFunc = sai_otn_device_api->remove_otn_device;
    m_setFunc = sai_otn_device_api->set_otn_device_attribute;
    m_getFunc = sai_otn_device_api->get_otn_device_attribute;

    RegisterNotifications();
}

bool OtnDeviceOrch::RegisterNotifications()
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr = {SAI_SWITCH_ATTR_OTN_ALARM_EVENT_NOTIFY, {0}};
    sai_status_t status = SAI_STATUS_SUCCESS;
    sai_attr_capability_t capability = {};

    status = sai_query_attribute_capability(gSwitchId, SAI_OBJECT_TYPE_SWITCH,
                                            SAI_SWITCH_ATTR_OTN_ALARM_EVENT_NOTIFY,
                                            &capability);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Unable to query the Otn Device event notification capability");
        return false;
    }

    if (!capability.set_implemented)
    {
        SWSS_LOG_INFO("Otn Device event notification not supported");
        return false;
    }

    /* Query the alarm event notification value before setting it */
    status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Unable to query the Otn Device event notification value");
        return false;
    }
    
    if (attr.value.ptr != nullptr)
    {
        SWSS_LOG_INFO("Otn Device event notification already set");
        return true;
    }

    attr.value.ptr = (void *)OnOtnDeviceAlarmNotification;

    status = sai_switch_api->set_switch_attribute(gSwitchId, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to register Otn Device event notification");
        return false;
    }

    SWSS_LOG_NOTICE("Otn Device event notification registered");

    return true;
}

void OtnDeviceOrch::OnOtnDeviceAlarmNotification(uint32_t count, const sai_otn_alarm_event_data_t *data)
{
    SWSS_LOG_ENTER();

    // TODO: Set LED status based on alarm severity
    SWSS_LOG_NOTICE("OTN alarm event notification count: %u", count);
    char hostname_buf[256] = {0};
    const char* nodename = nullptr;
    if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0 && hostname_buf[0] != '\0')
        nodename = hostname_buf;

    // Get real entity name from VID2NAME
    DBConnector counters_db("COUNTERS_DB", 0);
    Table vid2name_table(&counters_db, "VID2NAME");

    for (uint32_t i = 0; i < count; i++)
    {
        std::string oid_str = sai_serialize_object_id(data[i].object_id);
        std::string entity_display;
        if (!vid2name_table.hget("", oid_str, entity_display) || entity_display.empty())
        {
            SWSS_LOG_WARN("VID2NAME lookup failed!");
            entity_display = oid_str;
        }

        std::string line = formatOtnAlarmEventSyslog(&data[i], nodename, entity_display);
        if (!line.empty())
            SWSS_LOG_NOTICE("%s", line.c_str());
    }
}
