#include "clipboard_impl.h"

namespace composition {

void ClipboardImpl::clear() {
    items_.clear();
}

void ClipboardImpl::addItem(const ClipboardItem& item) {
    items_.push_back(item);
}

const std::vector<ClipboardItem>& ClipboardImpl::getItems() const {
    return items_;
}

bool ClipboardImpl::isEmpty() const {
    return items_.empty();
}

} // namespace composition
