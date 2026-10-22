// The on-screen gamepad of iOS, TheMoonPeople/Chronicle's Android overlay (ChronicleTouchOverlay.java) in
// UIKit: a view over the game's that draws the controls and takes every touch, so no draw call enters the
// game's frame. Its state reaches pad 0 through InputSetTouchOverlay.

#include "ios_touch_overlay.hpp"

#import <UIKit/UIKit.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "platform/input.hpp"

namespace {

// Fourteen buttons, then the left and the right stick.
constexpr int kControls = 16;
constexpr int kButtons = 14;

constexpr std::array<std::uint16_t, kButtons> kButtonBits = {
    kInputUp, kInputDown, kInputLeft, kInputRight, kInputCross, kInputCircle, kInputSquare,
    kInputTriangle, kInputL1, kInputR1, kInputL2, kInputR2, kInputSelect, kInputStart};

NSString *const kLabels[kButtons] = {@"↑", @"↓", @"←", @"→", @"×", @"○", @"□", @"△",
                                     @"L1", @"R1", @"L2", @"R2", @"SELECT", @"START"};

constexpr std::array<std::uint32_t, kButtons> kColours = {
    0xedf2ff, 0xedf2ff, 0xedf2ff, 0xedf2ff, 0x73b8ff, 0xff878b, 0xefa1e9,
    0x8be7a5, 0xedf2ff, 0xedf2ff, 0xedf2ff, 0xedf2ff, 0xedf2ff, 0xedf2ff};

UIColor *Colour(std::uint32_t argb) {
    return [UIColor colorWithRed:((argb >> 16) & 0xff) / 255.0
                           green:((argb >> 8) & 0xff) / 255.0
                            blue:(argb & 0xff) / 255.0
                           alpha:((argb >> 24) & 0xff) / 255.0];
}

CGFloat Clamp(CGFloat value, CGFloat lo, CGFloat hi) { return std::max(lo, std::min(hi, value)); }

} // namespace

@interface CTTouchOverlayView : UIView
@end

@implementation CTTouchOverlayView {
    CGFloat _cx[kControls], _cy[kControls], _radius[kControls];
    // Each touch keeps the control it went down on, as on Android: a finger that slides off a button
    // lets go of it, and one that went down on nothing presses nothing.
    NSMapTable<UITouch *, NSNumber *> *_captures;
    InputTouchOverlayState _last;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.opaque = NO;
        self.backgroundColor = UIColor.clearColor;
        self.multipleTouchEnabled = YES;
        self.contentMode = UIViewContentModeRedraw;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        _captures = [NSMapTable strongToStrongObjectsMapTable];
    }
    return self;
}

// Android's layout, in the safe area so the island and the home indicator cover nothing.
- (void)layoutSubviews {
    [super layoutSubviews];
    const CGRect  area = UIEdgeInsetsInsetRect(self.bounds, self.safeAreaInsets);
    const CGFloat x0 = area.origin.x, y0 = area.origin.y, w = area.size.width, h = area.size.height;
    if (w <= 0 || h <= 0) {
        return;
    }
    const CGFloat s = std::min(w, h);
    const CGFloat scale = std::min(w / 1280.0, h / 720.0);
    const CGFloat r = 46 * scale, small = 44 * scale, step = 104 * scale;
    const CGFloat side = Clamp(.08 + .08 * s / w, .10, .16);
    const CGFloat left = x0 + side * w, right = x0 + (1 - side) * w, row = y0 + h * .52;
    for (int i = 0; i < kControls; ++i) {
        _radius[i] = i < 12 ? r : i < 14 ? small : 80 * scale;
    }
    _cx[0] = _cx[1] = left;
    _cy[0] = row - step;
    _cy[1] = row + step;
    _cx[2] = left - step;
    _cx[3] = left + step;
    _cy[2] = _cy[3] = row;
    _cx[4] = _cx[7] = right;
    _cy[4] = row + step;
    _cy[7] = row - step;
    _cx[5] = right + step;
    _cx[6] = right - step;
    _cy[5] = _cy[6] = row;
    _cx[8] = _cx[10] = left;
    _cx[9] = _cx[11] = right;
    _cy[8] = _cy[9] = y0 + h * .22;
    _cy[10] = _cy[11] = y0 + h * .08;
    _cx[12] = x0 + w * .43;
    _cx[13] = x0 + w * .57;
    _cy[12] = _cy[13] = y0 + h * .92;
    _cx[14] = x0 + (side + .025) * w;
    _cx[15] = x0 + (1 - side - .025) * w;
    _cy[14] = _cy[15] = y0 + h * .86;
    [_captures removeAllObjects];
    [self publish];
    [self setNeedsDisplay];
}

- (CGFloat)distanceFrom:(int)control to:(CGPoint)p {
    return std::hypot(p.x - _cx[control], p.y - _cy[control]);
}

- (int)hit:(CGPoint)p {
    for (int i = 0; i < kControls; ++i) {
        if ([self distanceFrom:i to:p] <= _radius[i]) {
            return i;
        }
    }
    return -1;
}

- (void)publish {
    InputTouchOverlayState next{};
    if (!self.hidden) {
        for (UITouch *touch in _captures) {
            const int control = [_captures objectForKey:touch].intValue;
            const CGPoint p = [touch locationInView:self];
            if (control < 0) {
                continue;
            }
            if (control < kButtons) {
                if ([self distanceFrom:control to:p] <= _radius[control]) {
                    next.buttons |= kButtonBits[control];
                }
                continue;
            }
            CGFloat x = Clamp((p.x - _cx[control]) / _radius[control], -1, 1);
            CGFloat y = Clamp((p.y - _cy[control]) / _radius[control], -1, 1);
            const CGFloat magnitude = std::hypot(x, y);
            if (magnitude > 1) {
                x /= magnitude;
                y /= magnitude;
            }
            (control == 14 ? next.left_x : next.right_x) = static_cast<float>(x);
            (control == 14 ? next.left_y : next.right_y) = static_cast<float>(y);
        }
    }
    if (next.buttons != _last.buttons || next.left_x != _last.left_x || next.left_y != _last.left_y ||
        next.right_x != _last.right_x || next.right_y != _last.right_y) {
        _last = next;
        [self setNeedsDisplay];
    }
    InputSetTouchOverlay(next);
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [_captures setObject:@([self hit:[touch locationInView:self]]) forKey:touch];
    }
    [self publish];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self publish];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [_captures removeObjectForKey:touch];
    }
    [self publish];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
}

- (void)setControlsVisible:(BOOL)visible {
    if (self.hidden == !visible) {
        return;
    }
    self.hidden = !visible;
    if (!visible) {
        [_captures removeAllObjects];
    }
    [self publish];
}

- (void)disc:(CGContextRef)context x:(CGFloat)x y:(CGFloat)y r:(CGFloat)r fill:(std::uint32_t)fill
        edge:(std::uint32_t)edge {
    const CGRect circle = CGRectMake(x - r, y - r, 2 * r, 2 * r);
    CGContextSetFillColorWithColor(context, Colour(fill).CGColor);
    CGContextFillEllipseInRect(context, circle);
    CGContextSetStrokeColorWithColor(context, Colour(edge).CGColor);
    CGContextSetLineWidth(context, std::max<CGFloat>(1, r * .027));
    CGContextStrokeEllipseInRect(context, circle);
}

- (void)drawRect:(CGRect)rect {
    CGContextRef context = UIGraphicsGetCurrentContext();
    for (int i = kButtons; i < kControls; ++i) {
        [self disc:context x:_cx[i] y:_cy[i] r:_radius[i] fill:0x29202a3a edge:0x779fabc0];
        const CGFloat dx = i == 14 ? _last.left_x : _last.right_x, dy = i == 14 ? _last.left_y : _last.right_y;
        [self disc:context
                 x:_cx[i] + dx * _radius[i] * .45
                 y:_cy[i] + dy * _radius[i] * .45
                 r:_radius[i] * .37
              fill:0x552d374d
              edge:0xaac9d7f0];
    }
    for (int i = 0; i < kButtons; ++i) {
        const bool pressed = (_last.buttons & kButtonBits[i]) != 0;
        [self disc:context
                 x:_cx[i]
                 y:_cy[i]
                 r:_radius[i]
              fill:pressed ? 0x99283450 : 0x3c202a3a
              edge:pressed ? 0xe6d5e3ff : 0x99c9d7f0];
        const CGFloat size = _radius[i] * (i >= 12 ? .40 : i >= 8 ? .50 : .70);
        NSDictionary *attributes = @{
            NSFontAttributeName : [UIFont systemFontOfSize:size weight:UIFontWeightMedium],
            NSForegroundColorAttributeName : Colour(0xff000000 | kColours[i]),
        };
        const CGSize text = [kLabels[i] sizeWithAttributes:attributes];
        [kLabels[i] drawAtPoint:CGPointMake(_cx[i] - text.width / 2, _cy[i] - text.height / 2)
                 withAttributes:attributes];
    }
}

@end

@interface CTTouchOverlayDriver : NSObject
@property(nonatomic, strong) CTTouchOverlayView *view;
@end

@implementation CTTouchOverlayDriver

// The game's window comes and goes (the setup screen has one of its own), and SDL adds its Metal view
// after the window, so the overlay is placed on top of whichever window is key, every tick.
- (void)tick:(NSTimer *)timer {
    UIWindow *window = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) {
            continue;
        }
        for (UIWindow *candidate in static_cast<UIWindowScene *>(scene).windows) {
            if (candidate.isKeyWindow) {
                window = candidate;
            }
        }
    }
    UIView *host = window.rootViewController.view;
    if (host == nil) {
        return;
    }
    if (self.view == nil) {
        self.view = [[CTTouchOverlayView alloc] initWithFrame:host.bounds];
    }
    if (self.view.superview != host) {
        [self.view removeFromSuperview];
        self.view.frame = host.bounds;
        [host addSubview:self.view];
    } else if (host.subviews.lastObject != self.view) {
        [host bringSubviewToFront:self.view];
    }
    [self.view setControlsVisible:InputTouchOverlayVisible()];
}

@end

void IosTouchOverlayStart() {
    static CTTouchOverlayDriver *driver = [CTTouchOverlayDriver new];
    // The game pumps the main run loop from its own loop, which fires this between frames.
    NSTimer *timer = [NSTimer timerWithTimeInterval:0.1
                                             target:driver
                                           selector:@selector(tick:)
                                           userInfo:nil
                                            repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
}
