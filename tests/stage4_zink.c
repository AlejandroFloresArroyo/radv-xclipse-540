/*
 * Stage 4: OpenGL ES through Zink (the system Mesa 26.2.4) on top of the radv-xclipse Vulkan
 * driver, offscreen. EGL surfaceless platform, FBO with an RGBA8 renderbuffer: clear to blue,
 * draw one orange triangle, glReadPixels, check pixels and write stage4.ppm.
 * Run with: env -u LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE=zink VK_DRIVER_FILES=...
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 256

static GLuint
shader(GLenum type, const char *src)
{
   GLuint s = glCreateShader(type);
   glShaderSource(s, 1, &src, NULL);
   glCompileShader(s);
   GLint ok = 0;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[1024];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      printf("shader compile failed: %s\n", log);
   }
   return s;
}

int
main(void)
{
   PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
   if (!get_display) {
      printf("FAIL no eglGetPlatformDisplayEXT\n");
      return 1;
   }
   EGLDisplay dpy = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
   EGLint maj = 0, min = 0;
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
      printf("FAIL eglInitialize (0x%x)\n", eglGetError());
      return 1;
   }
   printf("EGL %d.%d vendor=%s\n", maj, min, eglQueryString(dpy, EGL_VENDOR));
   eglBindAPI(EGL_OPENGL_ES_API);
   const EGLint cattr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE};
   EGLConfig cfg;
   EGLint ncfg = 0;
   if (!eglChooseConfig(dpy, cattr, &cfg, 1, &ncfg) || ncfg < 1) {
      printf("FAIL eglChooseConfig (0x%x)\n", eglGetError());
      return 1;
   }
   const EGLint xattr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr);
   if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
      printf("FAIL context (0x%x)\n", eglGetError());
      return 1;
   }
   printf("GL_RENDERER = %s\nGL_VERSION  = %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

   GLuint rb, fb;
   glGenRenderbuffers(1, &rb);
   glBindRenderbuffer(GL_RENDERBUFFER, rb);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      printf("FAIL framebuffer incomplete\n");
      return 1;
   }

   const char *vs = "#version 300 es\n"
                    "const vec2 p[3] = vec2[](vec2(0.0, 0.75), vec2(0.75, -0.75), vec2(-0.75, -0.75));\n"
                    "void main() { gl_Position = vec4(p[gl_VertexID], 0.0, 1.0); }\n";
   const char *fs = "#version 300 es\nprecision mediump float;\nout vec4 c;\n"
                    "void main() { c = vec4(1.0, 0.5, 0.0, 1.0); }\n";
   GLuint prog = glCreateProgram();
   glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
   glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
   glLinkProgram(prog);
   GLint linked = 0;
   glGetProgramiv(prog, GL_LINK_STATUS, &linked);
   if (!linked) {
      printf("FAIL program link\n");
      return 1;
   }
   GLuint vao;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);

   glViewport(0, 0, W, H);
   glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glUseProgram(prog);
   glDrawArrays(GL_TRIANGLES, 0, 3);

   unsigned char *px = malloc(W * H * 4);
   memset(px, 0x55, W * H * 4);
   glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); /* glReadPixels waits for the GPU */
   printf("glGetError = 0x%x\n", glGetError());

   int orange = 0, blue = 0, other = 0;
   for (int i = 0; i < W * H; i++) {
      const unsigned char *p = px + i * 4;
      if (p[0] == 255 && p[1] >= 126 && p[1] <= 129 && p[2] == 0)
         orange++;
      else if (p[0] == 0 && p[1] == 0 && p[2] == 255)
         blue++;
      else
         other++;
   }
   printf("pixels: orange %d, blue %d, other %d (a correct triangle covers about 28%%)\n", orange, blue, other);
   FILE *f = fopen("stage4.ppm", "wb");
   if (f) {
      /* GL rows are bottom-up; write top-down. */
      fprintf(f, "P6 %d %d 255\n", W, H);
      for (int y = H - 1; y >= 0; y--)
         for (int x = 0; x < W; x++)
            fwrite(px + (y * W + x) * 4, 1, 3, f);
      fclose(f);
      printf("wrote stage4.ppm\n");
   }
   free(px);
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(dpy, ctx);
   eglTerminate(dpy);
   const int ok = orange > W * H / 5 && orange < W * H / 2 && blue > W * H / 3 && other < W * H / 50;
   printf(ok ? "PASS zink\n" : "FAIL zink (unexpected pixels)\n");
   return ok ? 0 : 3;
}
