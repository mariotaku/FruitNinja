#ifndef DOJO_SCREEN_H
#define DOJO_SCREEN_H

#include "screen.h"
#include "menu_button.h"
#include "tex_loader.h"

struct Game;

class DojoScreen : public Screen {
public:
    // State machine (matches original DojoScreen states)
    enum State {
        TRANSITION_IN = 0,
        IDLE = 1,
        TRANSITION_OUT_SHOP = 2,
        TRANSITION_OUT_ABOUT = 3,
        FADE_OUT_GAME = 6,
    };

    DojoScreen(Game& g);
    ~DojoScreen();

    void enter() override;
    void update(float dt) override;
    void draw(Renderer& r) override;
    void exit() override;
    void on_touch_down(float x, float y) override;
    void on_touch_up(float x, float y) override;

private:
    Game& game;
    State state;
    float alpha; // transition alpha 0..1

    // Lazy-created buttons (original pattern: field1_0x94, field2_0x98, field3_0x9c)
    MenuButton* play_button;
    MenuButton* shop_button;
    MenuButton* about_button;

    // Button textures
    GLuint play_tex, shop_tex, about_tex;
    TexImage play_img, shop_img, about_img;
    bool buttons_created;

    void create_buttons();
    void destroy_buttons();

    void on_play();
    void on_shop();
    void on_about();
};

#endif
