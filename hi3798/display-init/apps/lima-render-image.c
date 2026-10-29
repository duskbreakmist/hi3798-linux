/* SPDX-License-Identifier: MIT
 * Standalone GLES2 draw/readback probe; loads the existing armhf Mesa EGL.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef void *EGLDisplay; typedef void *EGLConfig; typedef void *EGLContext;
typedef void *EGLSurface; typedef int EGLint; typedef unsigned EGLBoolean;
#define LOAD(ret,name,args) ret (*name)args = (ret (*)args)dlsym(egl,#name); if (!name) {fprintf(stderr,"missing %s\n",#name);return 2;}
#define GL(ret,name,args) ret (*name)args=(ret (*)args)eglGetProcAddress(#name); if(!name) return 3
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    void *egl=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);
    if(!egl){fprintf(stderr,"%s\n",dlerror());return 1;}
    LOAD(void *,eglGetProcAddress,(const char *));
    LOAD(EGLBoolean,eglInitialize,(EGLDisplay,EGLint *,EGLint *));
    LOAD(EGLBoolean,eglBindAPI,(unsigned));
    LOAD(EGLBoolean,eglChooseConfig,(EGLDisplay,const EGLint *,EGLConfig *,EGLint,EGLint *));
    LOAD(EGLContext,eglCreateContext,(EGLDisplay,EGLConfig,EGLContext,const EGLint *));
    LOAD(EGLSurface,eglCreatePbufferSurface,(EGLDisplay,EGLConfig,const EGLint *));
    LOAD(EGLBoolean,eglMakeCurrent,(EGLDisplay,EGLSurface,EGLSurface,EGLContext));
    LOAD(EGLBoolean,eglTerminate,(EGLDisplay));
    LOAD(unsigned,eglGetError,(void));
    LOAD(const char *,eglQueryString,(EGLDisplay,EGLint));
    EGLDisplay (*platform)(unsigned,void *,const EGLint *)=(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display=platform?platform(0x31dd,NULL,NULL):NULL;
    EGLint major,minor,n;
    if(!display || !eglInitialize(display,&major,&minor)){fprintf(stderr,"EGL init failed %x\n",eglGetError());return 4;}
    EGLint attrs[]={0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3038};
    EGLConfig config;
    if(!eglBindAPI(0x30a0)||!eglChooseConfig(display,attrs,&config,1,&n)||!n) return 5;
    EGLint ca[]={0x3098,2,0x3038},pa[]={0x3057,256,0x3056,256,0x3038};
    EGLContext context=eglCreateContext(display,config,NULL,ca);
    EGLSurface surface=eglCreatePbufferSurface(display,config,pa);
    if(!context||!surface||!eglMakeCurrent(display,surface,surface,context)){fprintf(stderr,"EGL current %x\n",eglGetError());return 6;}
    GL(const unsigned char *,glGetString,(unsigned));
    GL(unsigned,glCreateShader,(unsigned));
    GL(void,glShaderSource,(unsigned,int,const char *const *,const int *));
    GL(void,glCompileShader,(unsigned));
    GL(void,glGetShaderiv,(unsigned,unsigned,int *));
    GL(unsigned,glCreateProgram,(void));
    GL(void,glAttachShader,(unsigned,unsigned));
    GL(void,glBindAttribLocation,(unsigned,unsigned,const char *));
    GL(void,glLinkProgram,(unsigned));
    GL(void,glGetProgramiv,(unsigned,unsigned,int *));
    GL(void,glUseProgram,(unsigned));
    GL(void,glViewport,(int,int,int,int));
    GL(void,glVertexAttribPointer,(unsigned,int,unsigned,unsigned char,int,const void *));
    GL(void,glEnableVertexAttribArray,(unsigned));
    GL(void,glDrawArrays,(unsigned,int,int));
    GL(void,glReadPixels,(int,int,int,int,unsigned,unsigned,void *));
    GL(unsigned,glGetError,(void));
    const char *renderer=(const char *)glGetString(0x1f01);
    printf("EGL_VENDOR=%s\nEGL_VERSION=%s\nEGL_CLIENT_APIS=%s\n",
        eglQueryString(display,0x3053),eglQueryString(display,0x3054),eglQueryString(display,0x308d));
    printf("GL_VENDOR=%s\nGL_VERSION=%s\nGLSL_VERSION=%s\n",
        glGetString(0x1f00),glGetString(0x1f02),glGetString(0x8b8c));
    const char *extensions=eglQueryString(display,0x3055);
    printf("EGL_EXT_image_dma_buf_import=%s\n",extensions&&strstr(extensions,"EGL_EXT_image_dma_buf_import")?"advertised":"not advertised");
    EGLBoolean (*queryDisplayAttrib)(EGLDisplay,EGLint,intptr_t *)=(void *)eglGetProcAddress("eglQueryDisplayAttribEXT");
    const char *(*queryDeviceString)(void *,EGLint)=(void *)eglGetProcAddress("eglQueryDeviceStringEXT");
    intptr_t device=0;
    if(queryDisplayAttrib && queryDeviceString && queryDisplayAttrib(display,0x322c,&device)) {
        const char *node=queryDeviceString((void *)device,0x3377);
        printf("EGL_DRM_RENDER_NODE=%s\n",node?node:"not reported");
    }
    printf("EGL %d.%d; GL_RENDERER=%s\n",major,minor,renderer?renderer:"NULL");
    if(!renderer || (!strstr(renderer,"Mali")&&!strstr(renderer,"lima")) || strstr(renderer,"llvmpipe")) return 7;
    const char *vs="attribute vec2 position; void main(){ gl_Position=vec4(position,0.,1.); }";
    const char *fs="precision mediump float; void main(){ gl_FragColor=vec4(gl_FragCoord.x/256.,gl_FragCoord.y/256.,0.25,1.); }";
    unsigned v=glCreateShader(0x8b31),f=glCreateShader(0x8b30),prog=glCreateProgram(); int ok=0;
    glShaderSource(v,1,&vs,NULL);glCompileShader(v);glGetShaderiv(v,0x8b81,&ok);if(!ok)return 8;
    glShaderSource(f,1,&fs,NULL);glCompileShader(f);glGetShaderiv(f,0x8b81,&ok);if(!ok)return 9;
    glAttachShader(prog,v);glAttachShader(prog,f);glBindAttribLocation(prog,0,"position");glLinkProgram(prog);
    glGetProgramiv(prog,0x8b82,&ok);if(!ok)return 10;
    glUseProgram(prog);glViewport(0,0,256,256);
    const float vertices[]={-1.,-1.,3.,-1.,-1.,3.};
    glVertexAttribPointer(0,2,0x1406,0,0,vertices);glEnableVertexAttribArray(0);
    glDrawArrays(4,0,3);
    unsigned char *pixels=malloc(256*256*4);
    if (!pixels) return 12;
    glReadPixels(0,0,256,256,0x1908,0x1401,pixels);
    unsigned char *pixel=pixels+(128*256+128)*4;
    unsigned error=glGetError();
    printf("GPU single-frame pixel=%u,%u,%u,%u error=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],error);
    int failed=error||pixel[0]<126||pixel[0]>131||pixel[1]<126||pixel[1]>131||pixel[2]<62||pixel[2]>66;
    if (!failed && argc>1) {
        FILE *file=fopen(argv[1],"wb");
        if (!file) { perror(argv[1]); return 13; }
        fprintf(file,"P6\n256 256\n255\n");
        for (int y=255;y>=0;y--)
            for (int x=0;x<256;x++)
                fwrite(pixels+(y*256+x)*4,1,3,file);
        fclose(file);
    }
    free(pixels);
    eglTerminate(display);
    return failed ? 11:0;
}
