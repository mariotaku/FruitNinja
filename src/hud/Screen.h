#ifndef SCREEN_H
#define SCREEN_H

struct Renderer;

class Screen {
public:
    virtual ~Screen() {}
    virtual void enter() = 0;
    virtual void update(float dt) = 0;
    virtual void draw(Renderer& r) = 0;
    virtual void exit() = 0;

    virtual void on_touch_down(float x, float y) { (void)x; (void)y; }
    virtual void on_touch_up(float x, float y) { (void)x; (void)y; }
};

#endif
