#pragma once
#include "iclipboard.h"

namespace composition {

class ClipboardImpl : public IClipboard {
public:
    void clear() override;
    void addItem(const ClipboardItem& item) override;
    const std::vector<ClipboardItem>& getItems() const override;
    bool isEmpty() const override;

private:
    std::vector<ClipboardItem> items_;
};

} // namespace composition
