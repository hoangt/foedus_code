/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/xct/epoch.hpp>
#include <ostream>
namespace foedus {
namespace xct {
std::ostream& operator<<(std::ostream& o, const Epoch& v) {
    if (v.is_valid()) {
        o << v.get_epoch();
    } else {
        o << "<INVALID>";
    }
    return o;
}
}  // namespace xct
}  // namespace foedus