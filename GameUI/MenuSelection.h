#pragma once

#include <cstddef>

namespace spyhunter::ui
{
class MenuSelection
{
public:
    explicit MenuSelection(std::size_t itemCount = 0) : itemCount_(itemCount) {}

    void setItemCount(std::size_t itemCount);
    void move(int direction);
    void select(std::size_t index);

    std::size_t index() const { return index_; }
    std::size_t itemCount() const { return itemCount_; }

private:
    std::size_t itemCount_ = 0;
    std::size_t index_ = 0;
};
}
