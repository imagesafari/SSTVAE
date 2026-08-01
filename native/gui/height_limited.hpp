// A widget whose height can be bounded from outside, on top of whatever
// bound it computes for itself.
//
// It exists for one requirement: the receive picture and the transmit
// canvas must be *the same size*, and each already caps itself at 4:3 of
// its own width. Two caps on one property means the last `resizeEvent`
// to run wins -- measured at 523 px against 480 px on panes of identical
// width, which is exactly the "close but not equal" the locked layout
// was chosen to end. So the external bound is passed in and combined
// with the internal one rather than overwriting it.
//
// An interface rather than a common base class because the two
// implementations have nothing else in common: one is a QLabel in a
// hand-positioned box, the other a painted editor.

#ifndef SSTVAE_GUI_HEIGHT_LIMITED_HPP
#define SSTVAE_GUI_HEIGHT_LIMITED_HPP

namespace sstvae::gui {

class HeightLimited {
public:
    virtual ~HeightLimited() = default;
    // Zero clears the bound. Implementations must combine this with
    // their own cap by taking the smaller, never by replacing it.
    virtual void set_height_limit(int limit) = 0;
};

}  // namespace sstvae::gui

#endif
