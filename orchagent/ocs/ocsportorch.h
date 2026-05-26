#pragma once

#include "objectorch.h"

class OcsPortOrch: public ObjectOrch
{
public:
    OcsPortOrch(DBConnector *db, const std::vector<std::string> &table_names);
    virtual void setFlexCounter(sai_object_id_t id) override;
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs);
};
