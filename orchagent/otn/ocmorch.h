#pragma once

#include "objectorch.h"

class OcmOrch: public ObjectOrch
{
public:
    OcmOrch(DBConnector *db, const std::vector<std::string> &table_names);

    bool handleRpcRequest(const std::string &op,
                          const std::string &data,
                          const std::vector<swss::FieldValueTuple> &inputs,
                          std::vector<swss::FieldValueTuple> &reply) override;

private:
    sai_status_t doGetOcmRaw(const std::string &data, std::vector<swss::FieldValueTuple> &values);
};


class OcmChannelOrch: public ObjectOrch
{
public:
    OcmChannelOrch(DBConnector *db, const std::vector<std::string> &table_names);
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs) override;
};
