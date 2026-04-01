#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <stdio.h>
#include <stdlib.h>

char * generate_dynamic_array_vs() {
    size_t buffer_size = 512 * 1024; 
    char *src = malloc(buffer_size);
    char *ptr = src;

    ptr += sprintf(ptr, "#version 310 es\n");
    ptr += sprintf(ptr, "layout(std430, binding = 0) buffer Data {\n");
    ptr += sprintf(ptr, "    int index1;\n");
    ptr += sprintf(ptr, "    int index2;\n");
    ptr += sprintf(ptr, "    float multiplier_odd;\n");
    ptr += sprintf(ptr, "    float multiplier_even;\n");
    ptr += sprintf(ptr, "    float result;\n");
    ptr += sprintf(ptr, "};\n");
    ptr += sprintf(ptr, "void main() {\n");

    // Declare a large local array to force spilling to scratch space.
    ptr += sprintf(ptr, "    float my_array[512];\n");

    // Fill the array
    ptr += sprintf(ptr, "    for (int i = 0; i < 512; i += 2) {\n");
    ptr += sprintf(ptr, "        my_array[i] = float(i) * multiplier_even;\n");
    ptr += sprintf(ptr, "        my_array[i + 1] = float(i + 1) * multiplier_odd;\n");
    ptr += sprintf(ptr, "    }\n");

    // Write to the SSBO
    ptr += sprintf(ptr, "    result = my_array[index1] + my_array[index2];\n");
    
    // Vertex shaders must write to gl_Position
    ptr += sprintf(ptr, "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n");
    ptr += sprintf(ptr, "}\n");

    return src;
}

const char *dummy_fs_source = 
    "#version 310 es\n"
    "precision mediump float;\n"
    "void main() {\n"
    "}\n";

struct buffer_data {
    int index1;
    int index2;
    float multiplier_odd;
    float multiplier_even;
    float result;
};

// Helper to catch silent compilation failures
void check_shader(GLuint shader, const char *name) {
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = malloc(log_len);
        glGetShaderInfoLog(shader, log_len, NULL, log);
        printf("[ERROR] Compiling %s:\n%s\n", name, log);
        free(log);
        exit(1);
    }
}

int main() {
    // 1. Headless EGL Setup
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig config;
    EGLint num_configs;
    EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs);
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE});
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

    // Sanity check: Does the driver allow SSBOs in the Vertex Shader?
    GLint max_vs_ssbos = 0;
    glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &max_vs_ssbos);
    printf("Driver Max VS SSBOs: %d\n", max_vs_ssbos);
    if (max_vs_ssbos == 0) {
        printf("[WARNING] This driver does NOT support SSBOs in the Vertex stage! The test will fail.\n");
    }

    // 2. Compile Vertex Shader
    char *vs_source = generate_dynamic_array_vs();
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, (const GLchar *const *)&vs_source, NULL);
    glCompileShader(vs);
    check_shader(vs, "Vertex Shader");
    free(vs_source);

    // 3. Compile Dummy Fragment Shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &dummy_fs_source, NULL);
    glCompileShader(fs);
    check_shader(fs, "Fragment Shader");
    
    // 4. Link Program
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        printf("[ERROR] Program linking failed!\n");
        exit(1);
    }
    glUseProgram(prog);

    // 5. Generate and Bind VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // 6. Fix for EGL_NO_SURFACE: Create a dummy Framebuffer Object (FBO)
    // Without this, the default FBO is incomplete and glDrawArrays gets silently aborted.
    GLuint fbo, tex;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1); // 1x1 dummy texture
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("[ERROR] Dummy Framebuffer is not complete!\n");
        exit(1);
    }

    // 7. Setup SSBO
    GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    
    struct buffer_data buffer_data = {
        .index1 = 15,
        .index2 = 468,
        .multiplier_odd = 4.0f,
        .multiplier_even = 2.0f,
        .result = 0.0f,
    };

    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(buffer_data), &buffer_data, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

    // 8. Execute the Pipeline
    printf("Dispatching Vertex stage...\n");
    glEnable(GL_RASTERIZER_DISCARD);
    glDrawArrays(GL_POINTS, 0, 1);
    
    // Catch pipeline errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("[ERROR] OpenGL error after draw call: 0x%04x\n", err);
    }
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 9. Verify Results
    struct buffer_data *out_buffer_data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct buffer_data), GL_MAP_READ_BIT);
    float result = out_buffer_data->result;
    float expected = 0;

    if (buffer_data.index1 % 2)
        expected += buffer_data.index1 * buffer_data.multiplier_odd;
    else
        expected += buffer_data.index1 * buffer_data.multiplier_even;

    if (buffer_data.index2 % 2)
        expected += buffer_data.index2 * buffer_data.multiplier_odd;
    else
        expected += buffer_data.index2 * buffer_data.multiplier_even;
    
    printf("\n--- Scratch Buffer Result ---\n");
    printf("GPU Computed Value: %f\n", result);
    printf("Expected Value: %f\n", expected);

    if (result == expected) {
        printf("RESULT: PASS\n");
    } else {
        printf("RESULT: FAIL\n");
    }

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    return 0;
}