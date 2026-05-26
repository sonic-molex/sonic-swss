#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <inttypes.h>
#include <stdexcept>
#include <sys/time.h>
#include <chrono>
#include "timestamp.h"
#include "objectorch.h"
#include "sai_serialize.h"
#include "flexcounterorch.h"
#include "converter.h"
#include "subscriberstatetable.h"
#include "redispipeline.h"
#include "tokenize.h"
#include "logger.h"
#include "consumerstatetable.h"
#include "orchfsm.h"
#include "redisapi.h"


extern sai_object_id_t gSwitchId;
extern FlexManagerDirectory g_FlexManagerDirectory;

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
    m_stateDb = std::shared_ptr<DBConnector>(new DBConnector("STATE_DB", 0));
    m_countersDb = std::shared_ptr<DBConnector>(new DBConnector("COUNTERS_DB", 0));
    m_vid2NameTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), "VID2NAME"));

    SWSS_LOG_NOTICE("ObjectOrch init, object type=%u, object name=%s", m_objectType, objectName);

    /* Initialize local data from meta data, save the short names to match openconfig keys */
    const sai_object_type_info_t *oi = sai_metadata_get_object_type_info(m_objectType);
    if (oi == NULL) {
        SWSS_LOG_ERROR("Invalid object type %u, object name=%s", m_objectType, objectName);
        return;
    }

    if (oi->enummetadata == NULL)
    {
        SWSS_LOG_ERROR("Object type %u has no enum metadata", m_objectType);
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

    /* Save stats values for flex counter */
    if (oi->statenum != NULL)
    {
        for (size_t index = 0; index < oi->statenum->valuescount; index++)
        {
            m_statValues[oi->statenum->valuesnames[index]] = oi->statenum->values[index];
        }
    }

    /* Set create and set attributes to state cache list */
    for (auto it : m_createandsetAttrs)
    {
        m_needToCache.insert(it.first);
    }
}

ObjectOrch::ObjectOrch(DBConnector *db,
    const std::vector<std::string>& table_names,
    sai_object_type_t obj_type,
    CounterType flex_counter_type)
    : Orch(db, table_names),
    m_objectType(obj_type),
    m_flex_counter_type(flex_counter_type),
    m_notificationConsumer(nullptr),
    m_notificationProducer(nullptr),
    m_flex_stat_manager(nullptr)
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
    m_notificationProducer(nullptr),
    m_flex_stat_manager(nullptr)
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

    if (OrchFSM::getState() != ORCH_STATE_WORK)
    {
        goto error;
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
#if 0
            if (m_irrecoverableAttrs.find(field) == m_irrecoverableAttrs.end())
            {
                SWSS_LOG_ERROR("Cannot use redis-channel to set recoverable attr, %s",
                               field.c_str());
                goto error;
            }
#endif
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
    for (auto fv: createonly_attrs)
    {
        sai_attribute_t attr;
        if (!translateObjectAttr(fv.first, fv.second, attr))
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

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), key.c_str());
        return false;
    }

    sai_object_id_t oid = m_key2oid[key];

    /* clean flex counter first then remove object */
    clearFlexCounter(oid);

    sai_status_t status = m_removeFunc(oid);
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

    if (m_stateTable)
    {
        m_stateTable->del(key);
    }

    return true;
}

bool ObjectOrch::bulkRemoveObjects(const std::vector<std::string> &keys)
{
    SWSS_LOG_ENTER();

    if (keys.empty()) return true;

    auto t0 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    uint32_t count = static_cast<uint32_t>(keys.size());
    std::vector<sai_object_id_t> oids(count);
    std::vector<sai_status_t> statuses(count);

    for (uint32_t i = 0; i < count; i++)
    {
        if (m_key2oid.find(keys[i]) == m_key2oid.end())
        {
            SWSS_LOG_ERROR("bulkRemove: key not found %s|%s", m_objectName.c_str(), keys[i].c_str());
            return false;
        }
        oids[i] = m_key2oid[keys[i]];
    }

    auto t1 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    /* Clean flex counters BEFORE removing SAI objects to avoid a race where
       syncd's flex-counter polling thread queries OIDs that were just removed,
       which causes a SIGSEGV in processFlexCounterEvent. The singular remove
       path already does this correctly (see removeObject above). */
    auto cf0 = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < count; i++)
    {
        clearFlexCounter(oids[i]);
    }
    auto cf1 = std::chrono::steady_clock::now();

    sai_status_t status = m_bulkRemoveFunc(
        count, oids.data(),
        SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
        statuses.data());

    auto t2 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    bool allOk = (status == SAI_STATUS_SUCCESS);

    // Batch vid/name/state Redis writes via pipelined Tables
    RedisPipeline batchVidPipe(m_countersDb.get(), 256);
    Table batchVid(&batchVidPipe, m_vid2NameTable->getTableName(), false);
    RedisPipeline batchNamePipe(m_countersDb.get(), 256);
    Table batchName(&batchNamePipe, m_nameMapTable->getTableName(), false);
    RedisPipeline batchStatePipe(m_stateDb.get(), 256);
    Table batchState(&batchStatePipe, m_stateTable->getTableName(), false);

    for (uint32_t i = 0; i < count; i++)
    {
        if (statuses[i] != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("bulkRemove: failed for %s|%s, rv=%d",
                m_objectName.c_str(), keys[i].c_str(), statuses[i]);
            allOk = false;
            continue;
        }

        std::string oid_str = sai_serialize_object_id(oids[i]);
        batchVid.hdel("", oid_str);
        batchName.hdel("", oid_str);
        m_keys.erase(keys[i]);
        m_key2oid.erase(keys[i]);
        m_key2createonlyAttrs.erase(keys[i]);
        m_key2createandsetAttrs.erase(keys[i]);
        batchState.del(keys[i]);
    }

    batchVidPipe.flush();
    batchNamePipe.flush();
    batchStatePipe.flush();
    auto cf2 = std::chrono::steady_clock::now();

    long long us_flex = std::chrono::duration_cast<std::chrono::microseconds>(cf1 - cf0).count();
    long long us_tables = std::chrono::duration_cast<std::chrono::microseconds>(cf2 - cf1).count();

    auto t3 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SWSS_LOG_NOTICE("bulkRemove %s: %u objects, status=%d, collect=%lldms, sai=%lldms, cleanup=%lldms, total=%lldms",
        m_objectName.c_str(), count, status,
        (long long)(t1-t0), (long long)(t2-t1), (long long)(t3-t2), (long long)(t3-t0));
    SWSS_LOG_NOTICE("bulkRemove cleanup detail: flex=%lldus, tables=%lldus",
        (long long)us_flex, (long long)us_tables);
    return allOk;
}

bool ObjectOrch::bulkCreateObjects(const std::vector<std::string> &keys, size_t batchTotal)
{
    SWSS_LOG_ENTER();

    if (keys.empty()) return true;

    if (batchTotal == 0)
        batchTotal = keys.size();

    auto t0 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    uint32_t count = static_cast<uint32_t>(keys.size());

    std::vector<std::vector<sai_attribute_t>> allAttrs(count);
    std::vector<const sai_attribute_t *> attrPtrs(count);
    std::vector<uint32_t> attrCounts(count);
    std::vector<sai_object_id_t> oids(count);
    std::vector<sai_status_t> statuses(count);

    for (uint32_t i = 0; i < count; i++)
    {
        const std::string &key = keys[i];
        std::vector<sai_attribute_t> attrs;

        // Include ALL attrs in the bulk create so the vendor SAI can
        // process them atomically under batch mode.
        std::map<std::string, std::string> &createonly_attrs = m_key2createonlyAttrs[key];
        for (auto &fv : createonly_attrs)
        {
            sai_attribute_t attr;
            if (!translateObjectAttr(fv.first, fv.second, attr))
            {
                SWSS_LOG_ERROR("bulkCreate: failed to translate attr %s|%s", m_objectName.c_str(), fv.first.c_str());
                continue;
            }
            attrs.push_back(attr);
        }

        addExtraAttrsOnCreate(key, attrs);

        std::map<std::string, std::string> &createandset_attrs = m_key2createandsetAttrs[key];
        for (auto &fv : createandset_attrs)
        {
            sai_attribute_t attr;
            if (!translateObjectAttr(fv.first, fv.second, attr))
            {
                SWSS_LOG_ERROR("bulkCreate: failed to translate createandset attr %s|%s", m_objectName.c_str(), fv.first.c_str());
                continue;
            }
            attrs.push_back(attr);
        }

        allAttrs[i] = std::move(attrs);
        attrPtrs[i] = allAttrs[i].data();
        attrCounts[i] = static_cast<uint32_t>(allAttrs[i].size());
    }

    auto t1 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sai_status_t status;
    if (count == 1 && batchTotal <= 1)
    {
        statuses[0] = m_createFunc(&oids[0], gSwitchId, attrCounts[0], attrPtrs[0]);
        status = statuses[0];
    }
    else
    {
        status = m_bulkCreateFunc(
            gSwitchId, count,
            attrCounts.data(), attrPtrs.data(),
            SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
            oids.data(), statuses.data());
    }

    auto t2 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    bool allOk = (status == SAI_STATUS_SUCCESS);

    // Batch name/vid table writes via pipelined Tables
    RedisPipeline batchNamePipe(m_countersDb.get(), 256);
    Table batchName(&batchNamePipe, m_nameMapTable->getTableName(), false);
    RedisPipeline batchVidPipe(m_countersDb.get(), 256);
    Table batchVid(&batchVidPipe, m_vid2NameTable->getTableName(), false);

    auto cp0 = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < count; i++)
    {
        const std::string &key = keys[i];
        if (statuses[i] != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("bulkCreate: failed for %s|%s, rv=%d",
                m_objectName.c_str(), key.c_str(), statuses[i]);
            allOk = false;
            continue;
        }

        m_key2oid[key] = oids[i];

        FieldValueTuple tuple(sai_serialize_object_id(oids[i]), key);
        std::vector<FieldValueTuple> fields;
        fields.push_back(tuple);

        batchName.set("", fields);
        batchVid.set("", fields);
    }

    batchNamePipe.flush();
    batchVidPipe.flush();
    auto cp1 = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < count; i++)
    {
        if (statuses[i] == SAI_STATUS_SUCCESS) setFlexCounter(oids[i]);
    }
    auto cp2 = std::chrono::steady_clock::now();

    long long us_tables = std::chrono::duration_cast<std::chrono::microseconds>(cp1 - cp0).count();
    long long us_flex = std::chrono::duration_cast<std::chrono::microseconds>(cp2 - cp1).count();

    auto t3 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SWSS_LOG_NOTICE("bulkCreate %s: %u objects, status=%d, collect=%lldms, sai=%lldms, post=%lldms, total=%lldms",
        m_objectName.c_str(), count, status,
        (long long)(t1-t0), (long long)(t2-t1), (long long)(t3-t2), (long long)(t3-t0));
    SWSS_LOG_NOTICE("bulkCreate post detail: tables=%lldus, flex=%lldus",
        (long long)us_tables, (long long)us_flex);
    return allOk;
}

std::vector<std::string> ObjectOrch::collectInvalidConnections()
{
    return {};
}

void ObjectOrch::cleanupInvalidConnections(const std::vector<std::string> &invalidKeys)
{
}

void ObjectOrch::publishOperationResult(std::string channel, sai_status_t status_code, std::string message)
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

    if (OrchFSM::getState() != ORCH_STATE_WORK)
    {
        SWSS_LOG_ERROR("Orch isn't in working status");
        return false;
    }

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), key.c_str());
        return false;
    }

    std::string error_msg;
    sai_status_t status = SAI_STATUS_SUCCESS;

    for (auto fv : field_values)
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

            if (m_needToCache.find(fv.first) != m_needToCache.end())
            {
                std::vector<FieldValueTuple> fvs;
                fvs.push_back(fv);
                m_stateTable->set(key, fvs);
            }
            error_msg = "Set " + key + " " + fv.first + " to " + fv.second;
        }

        publishOperationResult(channel, status, error_msg);
    }

    return rv;
}

bool ObjectOrch::translateObjectAttr(
    _In_ const std::string &field,
    _In_ const std::string &value,
    _Out_ sai_attribute_t &attr)
{
    if (m_createandsetAttrs.find(field) != m_createandsetAttrs.end())
    {
        attr.id = m_createandsetAttrs[field];
    }
    else if (m_createonlyAttrs.find(field) != m_createonlyAttrs.end())
    {
        attr.id = m_createonlyAttrs[field];
    }
    else
    {
        SWSS_LOG_ERROR("Unrecognized attr, %s|%s", m_objectName.c_str(), field.c_str());
        return false;
    }

    auto meta = sai_metadata_get_attr_metadata(m_objectType, attr.id);
    if (meta == NULL)
    {
        SWSS_LOG_THROW("Unable to get %s metadata, attr=%d", m_objectName.c_str(), attr.id);
    }

    /* Value translate: for enum attributes, resolve against the attribute's
       own enum metadata to avoid collisions between different enums that
       share a short name (e.g. POWERED_OFF in both config_status and
       oper_status).  Fall back to the global m_enumValues map otherwise. */
    std::string newValue(value);
    bool resolved = false;
    if (meta->isenum && meta->enummetadata != NULL)
    {
        std::string lowerValue(value);
        std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
        for (size_t i = 0; i < meta->enummetadata->valuescount; i++)
        {
            std::string shortName(meta->enummetadata->valuesshortnames[i]);
            if (shortName == value)
            {
                newValue = meta->enummetadata->valuesnames[i];
                resolved = true;
                break;
            }
            std::transform(shortName.begin(), shortName.end(), shortName.begin(), ::tolower);
            if (shortName == lowerValue)
            {
                newValue = meta->enummetadata->valuesnames[i];
                resolved = true;
                break;
            }
        }
    }
    if (!resolved && m_enumValues.find(value) != m_enumValues.end())
    {
        newValue = m_enumValues[value];
    }
    if (newValue != value)
    {
        SWSS_LOG_NOTICE("translateObjectAttr, field = %s, value = %s", field.c_str(), newValue.c_str());
    }

    try
    {
        sai_deserialize_attr_value(newValue, *meta, attr);
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Unrecongnized attr value, %s|%s|%s",
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
    if (!translateObjectAttr(field, value, attr))
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

    sai_status_t status;
    sai_attribute_t attr;
    if (m_readonlyAttrs.find(field) == m_readonlyAttrs.end())
    {
        SWSS_LOG_ERROR("Unsupported attr, %s|%s", m_objectName.c_str(), field.c_str());
        return SAI_STATUS_FAILURE;
    }
    attr.id = m_readonlyAttrs[field];

    status = m_getFunc(oid, 1, &attr);
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
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to serialize attr value, %s|%s|%s",
                       m_objectName.c_str(), field.c_str(), value.c_str());
        return SAI_STATUS_FAILURE;
    }
    SWSS_LOG_NOTICE("Get %s attr successed, pid:%" PRIx64 " field=%s, value=%s",
                    m_objectName.c_str(), oid, field.c_str(), value.c_str());

    return SAI_STATUS_SUCCESS;
}

void ObjectOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() <= ORCH_STATE_READY)
    {
        return;
    }

    if (consumer.getDbName() == "STATE_DB")
    {
        doStateTask(consumer);
        return;
    }

    bool useBulk = (m_bulkCreateFunc != nullptr && m_bulkRemoveFunc != nullptr);

    SWSS_LOG_NOTICE("doTask enter: %s m_toSync=%zu, useBulk=%s, m_key2oid=%zu",
        m_objectName.c_str(), consumer.m_toSync.size(),
        useBulk ? "yes" : "no", m_key2oid.size());

    std::vector<std::string> delKeys;
    std::vector<std::string> createKeys;
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> updateEntries;

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;
        std::string key = kfvKey(t);
        std::string op = kfvOp(t);

        SWSS_LOG_NOTICE("doTask: Table = %s, key = %s, op = %s", m_objectName.c_str(), key.c_str(), op.c_str());

        if (op == SET_COMMAND)
        {
            std::map<std::string, std::string> createonly_attrs;
            std::map<std::string, std::string> createandset_attrs;

            for (auto i : kfvFieldsValues(t))
            {
                if (m_createonlyAttrs.find(fvField(i)) != m_createonlyAttrs.end())
                {
                    createonly_attrs[fvField(i)] = fvValue(i);
                }
                else if (m_createandsetAttrs.find(fvField(i)) != m_createandsetAttrs.end())
                {
                    createandset_attrs[fvField(i)] = fvValue(i);
                    SWSS_LOG_NOTICE("ObjectOrch::doTask, key=%s, value=%s", fvField(i).c_str(), fvValue(i).c_str());
                }
            }

            addId(key, createonly_attrs);

            if (m_keys.find(key) == m_keys.end())
            {
                m_keys.insert(key);
                m_key2createandsetAttrs[key] = createandset_attrs;
                m_key2createonlyAttrs[key] = createonly_attrs;
            }

            if (m_key2oid.find(key) == m_key2oid.end())
            {
                createKeys.push_back(key);
            }
            else
            {
                updateEntries.emplace_back(key, createandset_attrs);
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Deleting %s", key.c_str());
            delKeys.push_back(key);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
        }

        it = consumer.m_toSync.erase(it);
    }

    auto tStart = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SWSS_LOG_NOTICE("doTask: %s %zu DEL, %zu create, %zu update, bulk=%s, t=%lld",
        m_objectName.c_str(), delKeys.size(), createKeys.size(), updateEntries.size(),
        useBulk ? "yes" : "no", (long long)tStart);

    if (useBulk)
    {
        if (!bulkRemoveObjects(delKeys))
        {
            SWSS_LOG_ERROR("bulkRemove failed");
        }

        size_t batchTotal = createKeys.size() + updateEntries.size();
        if (!bulkCreateObjects(createKeys, batchTotal))
        {
            SWSS_LOG_ERROR("bulkCreate failed");
        }
    }
    else
    {
        for (auto &key : delKeys)
        {
            if (!removeObject(key))
            {
                SWSS_LOG_ERROR("Failed to remove object, %s", key.c_str());
            }
        }

        for (auto &key : createKeys)
        {
            if (!createObject(key))
            {
                SWSS_LOG_THROW("Failed to create object");
            }
        }
    }

    for (auto &entry : updateEntries)
    {
        if (!setObjectAttrs(entry.first, entry.second, entry.first))
        {
            SWSS_LOG_ERROR("Failed to set attributes, %s", entry.first.c_str());
        }
    }

    auto tEnd = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    SWSS_LOG_NOTICE("doTask: %s SAI done, %zu DEL, %zu create, t=%lld, elapsed=%lldms",
        m_objectName.c_str(), delKeys.size(), createKeys.size(),
        (long long)tEnd, (long long)(tEnd - tStart));

    // Auto-clean: only after a forced-add (bulk create that could conflict
    // with pre-existing connections). Forced-add is used when:
    //   1. Bulk path was used
    //   2. At least one new entry was created via the bulk SAI API
    //   3. The overall batch had more than one entry (creates + updates),
    //      which causes bulkCreateObjects to use the bulk SAI path even for
    //      a single new key
    size_t totalBatch = createKeys.size() + updateEntries.size();
    if (useBulk && !createKeys.empty() && totalBatch > 1)
    {
        auto invalidKeys = collectInvalidConnections();
        if (!invalidKeys.empty())
        {
            cleanupInvalidConnections(invalidKeys);
        }
    }
}

void ObjectOrch::doStateTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        std::string key = kfvKey(t);
        std::string op = kfvOp(t);

        bool has_present_field = false;
        std::string present_value;

        SWSS_LOG_INFO("%s, key = %s, op = %s", m_objectName.c_str(), key.c_str(), op.c_str());

        if (m_key2oid.find(key) == m_key2oid.end())
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        for (auto i : kfvFieldsValues(t))
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
                SWSS_LOG_NOTICE("setCounterIdList 0x%lx, key = %s", id, key.c_str());
                setFlexCounter(id);
            }
            else if (present_value == "NOT_PRESENT")
            {
                SWSS_LOG_NOTICE("clearCounterIdList 0x%lx, key = %s", id, key.c_str());
                clearFlexCounter(id);
            }

            doSubobjectStateTask(key, present_value);
            m_key2present[key] = present_value;
        }

        it = consumer.m_toSync.erase(it);
    }
}

bool ObjectOrch::loadFlexCounterScript(
    _In_ const std::string& script_path,
    _In_ const std::string& plugin_field,
    _In_ const std::string& group_name,
    _In_ const StatsMode stats_mode,
    _In_ const uint polling_interval,
    _In_ const bool enabled)
{
    SWSS_LOG_ENTER();

    FieldValueTuple fv_stat;

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
    }

    m_flex_stat_manager = g_FlexManagerDirectory.createFlexCounterManager(
            group_name, stats_mode, polling_interval, enabled, fv_stat);

    return m_flex_stat_manager != nullptr;
}

void ObjectOrch::addId(const std::string &key, std::map<std::string, std::string> &attrs)
{
    /* Add attribute id by key, id format:
        |     BYTE 1     |     BYTE 2     |      BYTE 3     |     BYTE 4      |
        |   slot index   |  module index  | component index |     reserve     |
     */
    size_t seps = key.find('|');
    std::string name = seps == std::string::npos ? key : key.substr(0, seps);

    int slot_index = 0, module_index = 0;
    sscanf(name.c_str(), "%*[A-Za-z^-]%d-%d", &slot_index, &module_index);

    uint32_t id = ((slot_index & 0xFF) << 24) + ((module_index & 0xFF) << 16);

    attrs["id"] = std::to_string(id);
}

void ObjectOrch::setFlexCounter(sai_object_id_t id)
{
    SWSS_LOG_ENTER();

    if (m_flex_stat_manager == nullptr)
    {
        SWSS_LOG_WARN("Flex counter manager not initialized for %" PRIx64 "", id);
        return;
    }

    std::unordered_set<std::string> counter_stats;
    for (const auto& it : m_statValues) {
        counter_stats.emplace(it.first);
    }
    m_flex_stat_manager->setCounterIdList(id, m_flex_counter_type, counter_stats);
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
