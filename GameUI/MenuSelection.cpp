#include "MenuSelection.h"

namespace spyhunter::ui
{
void MenuSelection::setItemCount(const std::size_t itemCount)
{
    itemCount_ = itemCount;
    if (itemCount_ == 0) index_ = 0;
    else index_ %= itemCount_;
}

void MenuSelection::move(const int direction)
{
    if (itemCount_ == 0 || direction == 0) return;
    const auto count = static_cast<long long>(itemCount_);
    auto next = (static_cast<long long>(index_) + direction) % count;
    if (next < 0) next += count;
    index_ = static_cast<std::size_t>(next);
}

void MenuSelection::select(const std::size_t index)
{
    index_ = itemCount_ == 0 ? 0 : index % itemCount_;
}
}
