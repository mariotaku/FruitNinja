#include "renderer.h"
#include <cstdio>
#include <cmath>

static const char* vert_src =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char* frag_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.rgb, c.a * u_alpha);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "Shader error: %s\n", log);
    }
    return s;
}

bool Renderer::init() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, NULL, log);
        fprintf(stderr, "Link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_tex_loc = glGetUniformLocation(program, "u_tex");
    u_alpha_loc = glGetUniformLocation(program, "u_alpha");
    return true;
}

void Renderer::shutdown() {
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
}

GLuint Renderer::upload_texture(const TexImage& img) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    return tex;
}

void Renderer::draw_fullscreen_quad(GLuint tex, float alpha) {
    static const float verts[] = {
        // pos        uv
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
    };

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_tex_loc, 0);
    glUniform1f(u_alpha_loc, alpha);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void Renderer::draw_sprite(GLuint tex, float x, float y, float w, float h,
                           float angle, float alpha) {
    float cx = ((x + w * 0.5f) / FN_SCREEN_W) * 2.0f - 1.0f;
    float cy = ((y + h * 0.5f) / FN_SCREEN_H) * 2.0f - 1.0f;
    float hw = w / FN_SCREEN_W;
    float hh = h / FN_SCREEN_H;

    float cosA = cosf(angle);
    float sinA = sinf(angle);

    float corners[][2] = {
        {-hw, -hh}, { hw, -hh}, {-hw,  hh}, { hw,  hh}
    };
    float uvs[][2] = {{0,1},{1,1},{0,0},{1,0}};

    float verts[16];
    for (int i = 0; i < 4; i++) {
        float rx = corners[i][0] * cosA - corners[i][1] * sinA;
        float ry = corners[i][0] * sinA + corners[i][1] * cosA;
        verts[i * 4 + 0] = cx + rx;
        verts[i * 4 + 1] = cy + ry;
        verts[i * 4 + 2] = uvs[i][0];
        verts[i * 4 + 3] = uvs[i][1];
    }

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_tex_loc, 0);
    glUniform1f(u_alpha_loc, alpha);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}
