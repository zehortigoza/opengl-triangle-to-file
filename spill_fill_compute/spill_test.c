#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <stdio.h>
#include <stdlib.h>

char * generate_dynamic_array_shader() {
    size_t buffer_size = 512 * 1024; 
    char *src = malloc(buffer_size);
    char *ptr = src;

    ptr += sprintf(ptr, "#version 310 es\n");
    ptr += sprintf(ptr, "layout(local_size_x = 1) in;\n");
    ptr += sprintf(ptr, "layout(std430, binding = 0) buffer Data {\n");
    ptr += sprintf(ptr, "    int index1;\n");
    ptr += sprintf(ptr, "    int index2;\n");
    ptr += sprintf(ptr, "    float multiplier_odd;\n");
    ptr += sprintf(ptr, "    float multiplier_even;\n");
    ptr += sprintf(ptr, "    float result;\n");
    ptr += sprintf(ptr, "};\n");
    ptr += sprintf(ptr, "void main() {\n");

    // Declare a large local array. 
    // 512 floats = 2048 bytes per thread. In SIMD16, that demands 
    // 32KB of space. It WILL be pushed to scratch.
    ptr += sprintf(ptr, "    float my_array[512];\n");

    // Fill the array
    ptr += sprintf(ptr, "    for (int i = 0; i < 512; i += 2) {\n");
    ptr += sprintf(ptr, "        my_array[i] = float(i) * multiplier_even;\n");
    ptr += sprintf(ptr, "        my_array[i + 1] = float(i + 1) * multiplier_odd;\n");
    ptr += sprintf(ptr, "    }\n");

    ptr += sprintf(ptr, "    result = my_array[index1] + my_array[index2];\n");
    ptr += sprintf(ptr, "}\n");

    return src;
}

struct buffer_data {
    int index1;
    int index2;
    float multiplier_odd;
    float multiplier_even;
    float result;
};

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig config;
    EGLint num_configs;
    EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs);
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE});
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

    char *shader_source = generate_dynamic_array_shader();
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, (const GLchar *const *)&shader_source, NULL);
    
    printf("Compiling shader...\n");
    glCompileShader(cs);
    
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    glUseProgram(prog);
    free(shader_source);

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

    printf("Dispatching compute...\n");
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    struct buffer_data *out_buffer_data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct  buffer_data), GL_MAP_READ_BIT);
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
    
    printf("--- Scratch Buffer Result ---\n");
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
