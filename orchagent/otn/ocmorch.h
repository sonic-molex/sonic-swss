#pragma once

#include "objectorch.h"

class OcmOrch: public ObjectOrch
{
public:
    OcmOrch(DBConnector *db, const std::vector<std::string> &table_names);
};


class OcmChannelOrch: public ObjectOrch
{
public:
    OcmChannelOrch(DBConnector *db, const std::vector<std::string> &table_names);
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs) override;
};
