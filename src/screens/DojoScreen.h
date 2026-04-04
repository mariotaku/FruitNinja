#ifndef DOJO_SCREEN_H
#define DOJO_SCREEN_H

#include "hud/Screen.h"
#include "hud/MenuButton.h"
#include "Mesh.h"
#include "asset/tex_loader.h"

struct Game;

class DojoScreen : public Screen {
public:
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
    float alpha;

    // Buttons
    MenuButton* play_button;
    MenuButton* shop_button;
    MenuButton* about_button;
    GLuint play_tex, shop_tex, about_tex;
    TexImage play_img, shop_img, about_img;
    bool buttons_created;

public:
    static const int NUM_FRUITS = 3;
private:
    Mesh fruit_meshes[NUM_FRUITS];
    GLuint fruit_atlas_tex;
    float fruit_rotations[NUM_FRUITS];
    float ring_angle; // overall ring rotation

    void create_buttons();
    void destroy_buttons();
    void load_fruit_meshes();

    void on_play();
    void on_shop();
    void on_about();
};

#endif
