#pragma once

namespace spyhunter::ui
{
// Clock/value injection keeps held-stick repeat independent of SDL and testable.
class MenuAxisRepeat
{
public:
    MenuAxisRepeat(float threshold, double initialDelay, double repeatDelay)
        : threshold_(threshold), initialDelay_(initialDelay), repeatDelay_(repeatDelay) {}
    void setValue(float value) { value_ = value; }
    void reset() { value_ = 0; direction_ = 0; next_ = 0; }
    int poll(double now) {
        const int direction = value_ > threshold_ ? 1 : value_ < -threshold_ ? -1 : 0;
        if (direction == 0) { direction_ = 0; return 0; }
        if (direction != direction_) {
            direction_ = direction;
            next_ = now + initialDelay_;
            return direction;
        }
        if (now < next_) return 0;
        next_ = now + repeatDelay_;
        return direction;
    }
private:
    float threshold_, value_ = 0;
    double initialDelay_, repeatDelay_, next_ = 0;
    int direction_ = 0;
};
}
