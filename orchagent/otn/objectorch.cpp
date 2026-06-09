#include <algorithm>
#include <inttypes.h>

#include "objectorch.h"
#include "otnutil.h"
#include "sai_serialize.h"
#include "redisapi.h"


extern sai_object_id_t gSwitchId;

void ObjectOrch::localDataInit(DBConnector *db)
{
    SWSS_LOG_ENTER();

    const char *objectName = sai_metadata_get_object_type_name(m_objectType);
    if (objectName == NULL)
    {
        SWSS_LOG_ERROR("Invalid object type %u", m_objectType);
        return;
    }

    m_objectName = objectName;
    m_stateDb = std::make_shared<DBConnector>("STATE_DB", 0);
    m_countersDb = std::make_shared<DBConnector>("COUNTERS_DB", 0);
    m_vid2NameTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), "VID2NAME"));

    SWSS_LOG_NOTICE("ObjectOrch init, object type=%u, object name=%s", m_objectType, objectName);

    /* Initialize local data from meta data, save the short names to match openconfig keys */
    const sai_object_type_info_t *oi = sai_metadata_get_object_type_info(m_objectType);
    if (oi == NULL) {
        SWSS_LOG_ERROR("Invalid object type %u, object name=%s", m_objectType, objectName);
        return;
    }

    for (size_t index = 0; index < oi->enummetadata->valuescount; index++)
    {
        /**
         * Record the attribute short name and id.
         * The default name format from sai meta data is underline.
         */
        std::string name(oi->enummetadata->valuesshortnames[index]);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        /**
         * To compatible with both hyphen and underline naming.
         * e.g.
         * 1. leaf-name
         * 2. leaf_name
         */
        std::string hyphen_name(name);
        std::replace(hyphen_name.begin(), hyphen_name.end(), '_', '-');
        sai_attr_id_t id = oi->enummetadata->values[index];
        const sai_attr_metadata_t *const attr = oi->attrmetadata[index];

        SWSS_LOG_DEBUG("localDataInit, enum index=%zu, attr valueprecision: %zu", index, attr->valueprecision);
        // Save precision value for each attribuite if precision is valid.
        if (attr->valueprecision > 0) {
            m_attrPrecisions[name] = attr->valueprecision;
            m_attrPrecisions[hyphen_name] = attr->valueprecision;
        }

        // Save attribute id for each different type attribute.
        auto addAttrToMap = [&](std::map<std::string, sai_attr_id_t>& attr_map) {
            attr_map[name] = id;
            attr_map[hyphen_name] = id;
            return;
        };

        if (attr->ismandatoryoncreate)
        {
            addAttrToMap(m_mandatoryAttrs);
        }

        if (attr->iscreateonly)
        {
            addAttrToMap(m_createonlyAttrs);
        }
        else if (attr->iscreateandset)
        {
            addAttrToMap(m_createandsetAttrs);
        }
        else if (attr->isreadonly)
        {
            addAttrToMap(m_readonlyAttrs);

            /* add original name for flex counter */
            m_readonlyOrgAttrs[oi->enummetadata->valuesnames[index]] = id;
        }

        if (attr->isenum)
        {
            for (size_t i = 0; i < attr->enummetadata->valuescount; i++)
            {
                /* enum original name */
                std::string enum_name(attr->enummetadata->valuesshortnames[i]);
                m_enumValues[enum_name] = attr->enummetadata->valuesnames[i];

                /* support enum name with lower case */
                std::transform(enum_name.begin(), enum_name.end(), enum_name.begin(), ::tolower);
                m_enumValues[enum_name] = attr->enummetadata->valuesnames[i];
            }
        }
    }

    /* Set create and set, create only attributes to state cache list */
    for (const auto &it : m_createandsetAttrs)
    {
        m_needToCache.insert(it.first);
    }

    for (const auto &it : m_createonlyAttrs)
    {
        m_needToCache.insert(it.first);
    }

    SWSS_LOG_DEBUG("localDataInit, exit");
}

ObjectOrch::ObjectOrch(DBConnector *db,
    const std::vector<std::string>& table_names,
    sai_object_type_t obj_type,
    CounterType flex_counter_type) :
    Orch(db, table_names),
    m_objectType(obj_type),
    m_flex_counter_type(flex_counter_type),
    m_notificationConsumer(nullptr),
    m_notificationProducer(nullptr)
{
    SWSS_LOG_ENTER();

    localDataInit(db);
}

ObjectOrch::ObjectOrch(DBConnector *db,
    std::vector<TableConnector> &connectors,
    sai_object_type_t obj_type,
    CounterType flex_counter_type) :
    Orch(connectors),
    m_objectType(obj_type),
    m_flex_counter_type(flex_counter_type),
    m_notificationConsumer(nullptr),
    m_notificationProducer(nullptr)
{
    SWSS_LOG_ENTER();

    localDataInit(db);
}

void ObjectOrch::doTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    std::string op;
    std::string data;
    sai_status_t status;
    std::vector<swss::FieldValueTuple> values;
    sai_object_id_t oid = SAI_NULL_OBJECT_ID;

    if (&consumer != m_notificationConsumer)
    {
        return;
    }

    consumer.pop(op, data, values);

    if (m_key2oid.find(data) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), data.c_str());
        goto error;
    }

    oid = m_key2oid[data];

    if (op == "set")
    {
        for (unsigned i = 0; i < values.size(); i++)
        {
            std::string &value = fvValue(values[i]);
            std::string &field = fvField(values[i]);

            status = setObjectAttr(oid, field, value);
            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to set attr, field=%s, value=%s, status=%d",
                    field.c_str(), value.c_str(), status);
                goto error;
            }
        }
        op = "SUCCESS";
        m_notificationProducer->send(op, data, values);

        return;
    }
    else if (op == "get")
    {
        for (unsigned i = 0; i < values.size(); i++)
        {
            std::string &value = fvValue(values[i]);
            std::string &field = fvField(values[i]);

            status = getObjectAttr(oid, field, value);
            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to get attr, field=%s, status=%d",
                    field.c_str(), status);
                goto error;
            }
        }
        op = "SUCCESS";
        m_notificationProducer->send(op, data, values);

        return;
    }

error:
    op = "FAILED";
    m_notificationProducer->send(op, data, values);

    return;
}

bool ObjectOrch::createObject(const std::string &key)
{
    SWSS_LOG_ENTER();

    std::vector<sai_attribute_t> attrs;
    std::map<std::string, std::string> &createonly_attrs = m_key2createonlyAttrs[key];
    for (const auto &fv : createonly_attrs)
    {
        sai_attribute_t attr;
        if (translateObjectAttr(fv.first, fv.second, attr) == false)
        {
            SWSS_LOG_ERROR("Failed to translate attr, %s|%s",
                           m_objectName.c_str(), fv.first.c_str());
            continue;
        }
        attrs.push_back(attr);
    }

    addExtraAttrsOnCreate(key, attrs);

    sai_object_id_t oid;
    sai_status_t status = m_createFunc(&oid, gSwitchId, static_cast<uint32_t>(attrs.size()), attrs.data());
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to create %s|%s, rv=%d", m_objectName.c_str(), key.c_str(), status);
        return false;
    }

    /* Copy config to state */
    copyConfigToState(key, createonly_attrs);

    SWSS_LOG_NOTICE("Create %s|%s oid:%" PRIx64, m_objectName.c_str(), key.c_str(), oid);

    m_key2oid[key] = oid;

    if (!setObjectAttrs(key, m_key2createandsetAttrs[key]))
    {
        SWSS_LOG_ERROR("Failed to set fields, %s", key.c_str());
    }

    FieldValueTuple tuple(sai_serialize_object_id(oid), key);
    std::vector<FieldValueTuple> fields;
    fields.push_back(tuple);
    m_nameMapTable->set("", fields);

    m_vid2NameTable->set("", fields);

    setFlexCounter(oid);

    SWSS_LOG_NOTICE("Initialized %s", key.c_str());

    return true;
}

bool ObjectOrch::removeObject(const std::string &key)
{
    SWSS_LOG_ENTER();

    sai_status_t status;
    sai_object_id_t oid;

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), key.c_str());
        return false;
    }

    oid = m_key2oid[key];

    /* clean flex counter first then remove object */
    clearFlexCounter(oid);

    status = m_removeFunc(oid);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to remove %s|%s, rv=%d", m_objectName.c_str(), key.c_str(), status);
        return false;
    }

    SWSS_LOG_NOTICE("Remove %s|%s oid:%" PRIx64, m_objectName.c_str(), key.c_str(), oid);

    m_keys.erase(key);
    m_key2oid.erase(key);
    m_key2createonlyAttrs.erase(key);
    m_key2createandsetAttrs.erase(key);

    /* delete redis backed tables:
        m_vid2NameTable
        m_nameMapTable
    */
    std::string oid_str = sai_serialize_object_id(oid);
    m_vid2NameTable->hdel("", oid_str);
    m_nameMapTable->hdel("", oid_str);

    return true;
}

void ObjectOrch::publishOperationResult(const std::string &channel, sai_status_t status_code, const std::string &message)
{
    swss::NotificationProducer notifications(m_stateDb.get(), channel);
    std::vector<swss::FieldValueTuple> entry;
    auto sent_clients = notifications.send(std::to_string(status_code), message, entry);
    SWSS_LOG_NOTICE("publishresult %d, %s to %ld client on channel %s",
            status_code, message.c_str(), sent_clients, channel.c_str());
}

bool ObjectOrch::setObjectAttrs(const std::string& key, std::map<std::string, std::string>& field_values, std::string operation_id)
{
    SWSS_LOG_ENTER();

    bool rv = true;

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), key.c_str());
        return false;
    }

    std::string error_msg;
    sai_status_t status = SAI_STATUS_SUCCESS;

    for (const auto &fv : field_values)
    {
        std::string channel = fv.first + "-" + operation_id;

        SWSS_LOG_NOTICE("set field=%s value=%s", fv.first.c_str(), fv.second.c_str());

        status = setObjectAttr(m_key2oid[key], fv.first, fv.second);
        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to set %s|%s %s to %s, status=%d",
                m_objectName.c_str(),
                key.c_str(),
                fv.first.c_str(),
                fv.second.c_str(),
                status);

            rv = false;
            error_msg = "Failed to set " + key + " " + fv.first + " to " + fv.second;
        }
        else
        {
            SWSS_LOG_NOTICE("Set %s|%s %s to %s",
                m_objectName.c_str(),
                key.c_str(),
                fv.first.c_str(),
                fv.second.c_str());

            copyConfigToState(key, fv);

            error_msg = "Set " + key + " " + fv.first + " to " + fv.second;
        }

        publishOperationResult(channel, status, error_msg);
    }

    return rv;
}

bool ObjectOrch::translateObjectAttr(
    _In_ const std::string &field,
    _In_ const std::string &value,
    _Out_ sai_attribute_t &attr) const
{
    if (m_createandsetAttrs.find(field) != m_createandsetAttrs.end())
    {
        attr.id = m_createandsetAttrs.at(field);
    }
    else if (m_createonlyAttrs.find(field) != m_createonlyAttrs.end())
    {
        attr.id = m_createonlyAttrs.at(field);
    }
    else
    {
        SWSS_LOG_ERROR("Unrecognized attr, %s|%s", m_objectName.c_str(), field.c_str());
        return false;
    }

    auto meta = sai_metadata_get_attr_metadata(m_objectType, attr.id);
    if (meta == nullptr)
    {
        SWSS_LOG_THROW("Unable to get %s metadata, attr=%d", m_objectName.c_str(), attr.id);
    }

    /* Value translate */
    std::string newValue(value);
    if (m_enumValues.find(value) != m_enumValues.end())
    {
        newValue = m_enumValues.at(value);
    }
    else if (m_attrPrecisions.find(field) != m_attrPrecisions.end())
    {
        try
        {
            newValue = otn::precisionEncode(value, m_attrPrecisions.at(field));
        }
        catch (...)
        {
            SWSS_LOG_ERROR("Failed to encode precision value, %s|%s|%s",
                           m_objectName.c_str(), field.c_str(), value.c_str());
            return false;
        }
    }

    SWSS_LOG_NOTICE("translateObjectAttr, field = %s, value = %s", field.c_str(), newValue.c_str());

    try
    {
        sai_deserialize_attr_value(newValue, *meta, attr);
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Unrecognized attr value, %s|%s|%s",
                       m_objectName.c_str(), field.c_str(), newValue.c_str());
        return false;
    }

    return true;
}

sai_status_t ObjectOrch::setObjectAttr(
    sai_object_id_t oid,
    const std::string &field,
    const std::string &value)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;
    if (translateObjectAttr(field, value, attr) == false)
    {
        SWSS_LOG_ERROR("Failed to translate attr, %s|%s",
                       m_objectName.c_str(), field.c_str());
        return SAI_STATUS_FAILURE;
    }

    sai_status_t status = m_setFunc(oid, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set %s attr, field=%s, value=%s, status=%d",
                       m_objectName.c_str(), field.c_str(), value.c_str(), status);
        return status;
    }

    SWSS_LOG_NOTICE("Set %s attr, pid:%" PRIx64 " field=%s, value=%s",
                     m_objectName.c_str(), oid, field.c_str(), value.c_str());

    return SAI_STATUS_SUCCESS;
}

sai_status_t ObjectOrch::getObjectAttr(sai_object_id_t oid, const std::string &field, std::string &value)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;
    auto readonlyAttr = m_readonlyAttrs.find(field);
    if (readonlyAttr == m_readonlyAttrs.end())
    {
        SWSS_LOG_ERROR("Unsupported attr, %s|%s", m_objectName.c_str(), field.c_str());
        return SAI_STATUS_FAILURE;
    }
    attr.id = readonlyAttr->second;

    sai_status_t status = m_getFunc(oid, 1, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to get %s attr, field=%s, status=%d",
                       m_objectName.c_str(), field.c_str(), status);
        return status;
    }
    auto meta = sai_metadata_get_attr_metadata(m_objectType, attr.id);
    if (meta == NULL)
    {
        SWSS_LOG_ERROR("Unable to get %s metadata, attr=%d", m_objectName.c_str(), attr.id);
        return SAI_STATUS_FAILURE;
    }

    try
    {
        value = sai_serialize_attr_value(*meta, attr, false);

        auto precision = m_attrPrecisions.find(field);
        if (precision != m_attrPrecisions.end())
        {
            value = otn::precisionDecode(value, precision->second);
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to get attr value, %s|%s|%s",
                       m_objectName.c_str(), field.c_str(), value.c_str());
        return SAI_STATUS_FAILURE;
    }

    SWSS_LOG_NOTICE("Get %s attr succeeded, pid:%" PRIx64 " field=%s, value=%s",
                    m_objectName.c_str(), oid, field.c_str(), value.c_str());

    return SAI_STATUS_SUCCESS;
}

void ObjectOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    if (consumer.getDbName() == "STATE_DB")
    {
        doStateTask(consumer);
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        std::string key = kfvKey(t);
        std::string op = kfvOp(t);

        SWSS_LOG_NOTICE("doTask: Table = %s, key = %s, op = %s", m_objectName.c_str(), key.c_str(), op.c_str());

        if (op == SET_COMMAND)
        {
            std::string operation_id = key;

            std::map<std::string, std::string> createonly_attrs;
            std::map<std::string, std::string> createandset_attrs;

            for (const auto &i : kfvFieldsValues(t))
            {
                const auto &name = fvField(i);
                if (m_createonlyAttrs.find(name) != m_createonlyAttrs.end())
                {
                    createonly_attrs[name] = fvValue(i);
                }
                else if (m_createandsetAttrs.find(name) != m_createandsetAttrs.end())
                {
                    createandset_attrs[name] = fvValue(i);
                    SWSS_LOG_NOTICE("ObjectOrch::doTask, key=%s, value=%s", name.c_str(), fvValue(i).c_str());
                }
            }

            // Add attribute name
            createonly_attrs["name"] = key;

            if (m_keys.find(key) == m_keys.end())
            {
                //Add create only attribute.
                m_keys.insert(key);
                m_key2createandsetAttrs[key] = createandset_attrs;
                m_key2createonlyAttrs[key] = createonly_attrs;
            }

            it = consumer.m_toSync.erase(it);

            /* Create object if needed */
            if (m_key2oid.find(key) == m_key2oid.end())
            {
                if (!createObject(key))
                {
                    SWSS_LOG_THROW("Failed to create object");
                }

                continue;
            }

            if (!setObjectAttrs(key, createandset_attrs, operation_id))
            {
                SWSS_LOG_ERROR("Failed to set attributes, %s", key.c_str());
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Deleting %s", key.c_str());
            if (!removeObject(key))
            {
                SWSS_LOG_ERROR("Failed to remove object, %s", key.c_str());
            }

            it = consumer.m_toSync.erase(it);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
            it = consumer.m_toSync.erase(it);
        }
    }

    if (m_flex_stat_manager != nullptr)
    {
        m_flex_stat_manager->flush();
    }
}

void ObjectOrch::doStateTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    // TODO, need to solve present state.

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        std::string key = kfvKey(t);
        std::string op = kfvOp(t);

        bool has_present_field = false;
        std::string present_value;

        SWSS_LOG_DEBUG("%s, key = %s, op = %s", m_objectName.c_str(), key.c_str(), op.c_str());

        if (m_key2oid.find(key) == m_key2oid.end())
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        for (const auto &i : kfvFieldsValues(t))
        {
            if (fvField(i) == "present")
            {
                has_present_field = true;
                present_value = fvValue(i);
                break;
            }
        }
        if (has_present_field == false)
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        sai_object_id_t id = m_key2oid[key];

        std::string present;

        if (m_key2present.find(key) != m_key2present.end())
        {
            present = m_key2present[key];
        }

        if (present_value != present)
        {
            if (present_value == "PRESENT")
            {
                SWSS_LOG_NOTICE("setCounterIdList 0x%" PRIx64 ", key = %s", id, key.c_str());
                setFlexCounter(id);
            }
            else if (present_value == "NOT_PRESENT")
            {
                SWSS_LOG_NOTICE("clearCounterIdList 0x%" PRIx64 ", key = %s", id, key.c_str());
                clearFlexCounter(id);
            }

            doSubobjectStateTask(key, present_value);
            m_key2present[key] = present_value;
        }

        it = consumer.m_toSync.erase(it);
    }
}

void ObjectOrch::createFlexCounter(
    _In_ const std::string& script_path,
    _In_ const std::string& plugin_field,
    _In_ const std::string& group_name,
    _In_ const StatsMode stats_mode,
    _In_ const uint polling_interval,
    _In_ const bool enabled)
{
    SWSS_LOG_ENTER();

    FieldValueTuple fv_stat = std::make_pair("","");

    if (!script_path.empty())
    {
        try
        {
            std::string path("/usr/share/sonic/platform/");
            path += script_path;
            std::string att_script = swss::readTextFile(path);
            std::string att_sha = swss::loadRedisScript(m_countersDb.get(), att_script);
            fv_stat = FieldValueTuple(plugin_field, att_sha);
        }
        catch (const std::runtime_error &e)
        {
            SWSS_LOG_WARN("%s group plugins was not set successfully: %s", group_name.c_str(), e.what());
            fv_stat = std::make_pair("","");
        }
    }

    m_flex_stat_manager = std::make_unique<FlexCounterTaggedCachedManager<void>>(
            group_name, stats_mode, polling_interval, enabled, fv_stat);
}

void ObjectOrch::setFlexCounter(sai_object_id_t id)
{
    SWSS_LOG_ENTER();

    if (m_flex_stat_manager == nullptr)
    {
        SWSS_LOG_WARN("Flex counter manager not initialized for %" PRIx64 "", id);
        return;
    }

    std::unordered_set<std::string> counter_attrs;
    for (const auto& it : m_readonlyOrgAttrs) {
        counter_attrs.emplace(it.first);
    }
    m_flex_stat_manager->setCounterIdList(id, m_flex_counter_type, counter_attrs);
}

void ObjectOrch::clearFlexCounter(sai_object_id_t id) {
    SWSS_LOG_ENTER();

    if (m_flex_stat_manager == nullptr)
    {
        SWSS_LOG_WARN("Flex counter manager not initialized");
        return;
    }

    SWSS_LOG_NOTICE("Clear flex counter, id: %" PRIx64 "", id);
    m_flex_stat_manager->clearCounterIdList(id);
}

void ObjectOrch::copyConfigToState(const std::string &key, const FieldValueTuple &fv)
{
    if (m_needToCache.find(fv.first) != m_needToCache.end())
    {
        std::vector<FieldValueTuple> fvs;
        fvs.push_back(fv);
        m_stateTable->set(key, fvs);
    }
}

void ObjectOrch::copyConfigToState(const std::string &key, std::map<std::string, std::string> &fvs)
{
    for (const auto &fv : fvs)
    {
        copyConfigToState(key, fv);
    }
}
