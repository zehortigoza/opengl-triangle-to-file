#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

// Width and height of the output image
#define WIDTH 640
#define HEIGHT 480

// --- Shader Sources ---

// Vertex Shader: Using GLSL 1.30 (OpenGL 3.0)
const char* vertex_shader_text =
"#version 130\n"
"uniform vec4 u_Vertices[3];\n"
"void main()\n"
"{\n"
"    // Positions now come from a uniform array\n"
"    gl_Position = u_Vertices[gl_VertexID];\n"
"}\n";

// Fragment Shader
const char* fragment_shader_text =
"#version 130\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"    // Hardcoded color\n"
"    FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n";

// --- Helper Functions ---

/**
 * Saves the RGB buffer to a PPM file (P6 format).
 */
void save_ppm(const char *filepath, int width, int height, unsigned char *pixels) {
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filepath);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    
    // OpenGL origin is bottom-left, but PPM expects top-left.
    // We write the rows in reverse order to flip the image.
    for (int y = height - 1; y >= 0; y--) {
        fwrite(&pixels[y * width * 3], 1, width * 3, f);
    }
    
    fclose(f);
    printf("Saved image to %s\n", filepath);
}

// Check shader compilation errors
void check_shader_compile(GLuint shader) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compile Error: %s\n", infoLog);
    }
}

int main(void) {
    // 1. Initialize EGL
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return -1;
    }

    // 2. Choose Configuration
    // We need PBUFFER support for off-screen rendering
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)) {
        fprintf(stderr, "Failed to choose EGL config\n");
        return -1;
    }

    // 3. Bind OpenGL API (vs OpenGL ES)
    eglBindAPI(EGL_OPENGL_API);

    // 4. Create EGL Context
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return -1;
    }

    // 5. Create Pbuffer Surface (The "Off-screen Window")
    // We need a surface to make the context current, even if we render to an FBO.
    EGLint pbufferAttribs[] = {
        EGL_WIDTH, WIDTH,
        EGL_HEIGHT, HEIGHT,
        EGL_NONE,
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        return -1;
    }

    // 6. Make Context Current
    if (!eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "Failed to make context current\n");
        return -1;
    }

    // --- FBO SETUP (Fix for ReadBuffer Errors) ---
    // We will render to a Framebuffer Object (FBO) instead of the default Pbuffer.
    // This gives us full control over the buffer structure.
    
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Create a texture to attach to the FBO
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    // Allocate texture memory (RGB)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WIDTH, HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Attach texture to the FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    // Check FBO status
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Error: Framebuffer is not complete!\n");
        return -1;
    }

    // --- Standard OpenGL Rendering Code Below ---

    // 7. Compile Vertex Shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertex_shader_text, NULL);
    glCompileShader(vertexShader);
    check_shader_compile(vertexShader);

    // 8. Compile Fragment Shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragment_shader_text, NULL);
    glCompileShader(fragmentShader);
    check_shader_compile(fragmentShader);

    // 9. Link Program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Clean up individual shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glUseProgram(shaderProgram);

    GLint verticesLoc = glGetUniformLocation(shaderProgram, "u_Vertices");

    GLfloat vertices[] = {
         0.0f,  0.5f, 0.0f, 1.0f, // Top
        -0.5f, -0.5f, 0.0f, 1.0f, // Bottom Left
         0.5f, -0.5f, 0.0f, 1.0f  // Bottom Right
    };

    if (verticesLoc != -1) {
        glUniform4fv(verticesLoc, 3, vertices);
    } else {
        fprintf(stderr, "Warning: Could not find u_Vertices uniform location.\n");
    }

    // 10. Render
    glViewport(0, 0, WIDTH, HEIGHT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Dummy VAO for Core Profile/Modern GL compatibility
    GLuint VAO;
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 11. Read Pixels from FBO
    // Since we are bound to an FBO, glReadPixels reads from GL_COLOR_ATTACHMENT0 by default.
    unsigned char *pixels = (unsigned char*)malloc(WIDTH * HEIGHT * 3);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // 12. Save to file
    save_ppm("output.ppm", WIDTH, HEIGHT, pixels);

    // 13. Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(shaderProgram);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    free(pixels);

    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);

    return 0;
}
