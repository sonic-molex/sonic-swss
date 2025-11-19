#pragma once

#include "objectorch.h"

class AttenuatorOrch: public ObjectOrch
{
public:
    AttenuatorOrch(DBConnector *db, const std::vector<std::string> &table_names);
};
