#include "Renderer.h"
#include "Mesh.h"
#include <cstdio>
#include <cmath>

// 2D shader with MVP projection and RGBA tint (matches TintColour pipeline)
static const char* vert_src =
    "attribute vec3 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char* frag_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_tint;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = c * u_tint;\n"
    "}\n";

// 3D mesh shaders
static const char* vert_3d_src =
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform vec3 u_light_dir;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "varying float v_light;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    vec3 wn = normalize(mat3(u_model) * a_normal);\n"
    "    v_light = max(dot(wn, u_light_dir), 0.0) * 0.6 + 0.4;\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char* frag_3d_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "varying float v_light;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv) * v_color;\n"
    "    gl_FragColor = vec4(c.rgb * v_light, c.a * u_alpha);\n"
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

    u_mvp = glGetUniformLocation(program, "u_mvp");
    u_tex_loc = glGetUniformLocation(program, "u_tex");
    u_tint = glGetUniformLocation(program, "u_tint");

    // 3D shader
    GLuint vs3 = compile_shader(GL_VERTEX_SHADER, vert_3d_src);
    GLuint fs3 = compile_shader(GL_FRAGMENT_SHADER, frag_3d_src);
    program_3d = glCreateProgram();
    glAttachShader(program_3d, vs3);
    glAttachShader(program_3d, fs3);
    glBindAttribLocation(program_3d, 0, "a_pos");
    glBindAttribLocation(program_3d, 1, "a_normal");
    glBindAttribLocation(program_3d, 2, "a_color");
    glBindAttribLocation(program_3d, 3, "a_uv");
    glLinkProgram(program_3d);
    ok = 0;
    glGetProgramiv(program_3d, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program_3d, 512, NULL, log);
        fprintf(stderr, "3D Link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs3);
    glDeleteShader(fs3);

    u3d_mvp = glGetUniformLocation(program_3d, "u_mvp");
    u3d_model = glGetUniformLocation(program_3d, "u_model");
    u3d_light_dir = glGetUniformLocation(program_3d, "u_light_dir");
    u3d_tex = glGetUniformLocation(program_3d, "u_tex");
    u3d_alpha = glGetUniformLocation(program_3d, "u_alpha");

    return true;
}

void Renderer::shutdown() {
    if (program) { glDeleteProgram(program); program = 0; }
    if (program_3d) { glDeleteProgram(program_3d); program_3d = 0; }
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

void Renderer::SetupGameOrtho() {
    // Verified from binary: SetupOrtho(160, -160, -240, 240, 2000, -6000)
    matrix_mgr.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
    matrix_mgr.view.Identity();
    matrix_mgr.stack.Reset();
}

void Renderer::draw_fullscreen_quad(GLuint tex, float alpha) {
    // Legacy clip-space fullscreen quad (for compatibility with existing screens)
    static const float verts[] = {
        // pos(xyz)       uv
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
    };

    Matrix44 identity;
    glUseProgram(program);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, identity.ptr());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_tex_loc, 0);
    glUniform4f(u_tint, 1.0f, 1.0f, 1.0f, alpha);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, verts + 3);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void Renderer::DrawQuad(const Colour& tint, float u0, float v0, float u1, float v1) {
    // Unit quad (-0.5..0.5) transformed by current matrix stack MVP
    float verts[] = {
        // pos(xyz)          uv
        -0.5f, -0.5f, 0.0f,  u0, v1,
         0.5f, -0.5f, 0.0f,  u1, v1,
        -0.5f,  0.5f, 0.0f,  u0, v0,
         0.5f,  0.5f, 0.0f,  u1, v0,
    };

    Matrix44 mvp = matrix_mgr.GetMVP();
    float tintF[4];
    tint.toFloat(tintF);

    glUseProgram(program);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.ptr());
    glUniform1i(u_tex_loc, 0);
    glUniform4f(u_tint, tintF[0], tintF[1], tintF[2], tintF[3]);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, verts + 3);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void Renderer::draw_mesh(Mesh& mesh, GLuint tex, const float* mvp, const float* model,
                         float alpha) {
    glUseProgram(program_3d);
    glUniformMatrix4fv(u3d_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(u3d_model, 1, GL_FALSE, model);
    glUniform3f(u3d_light_dir, 0.4f, 0.7f, 0.6f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u3d_tex, 0);
    glUniform1f(u3d_alpha, alpha);
    mesh.draw();
}

void Renderer::draw_sprite(GLuint tex, float x, float y, float w, float h,
                           float angle, float alpha) {
    // Set up matrix: Scale by (w, h) then translate to (x + w/2, y + h/2)
    matrix_mgr.stack.Reset();
    Matrix44 mat = Matrix44::Scale44(w, h, 1.0f);
    if (angle != 0.0f) {
        mat.RotZ44(sinf(angle), cosf(angle));
    }
    mat.GlobalTranslate44(Vec3(x + w * 0.5f, y + h * 0.5f, 0.0f));
    matrix_mgr.stack.SetCurrentMatrix(mat);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    DrawQuad(Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)));
}
