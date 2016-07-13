#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <clocale>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <fftw3.h>
#include <mad.h>
#include <string>

using namespace std;

const double rate24_16 = 0x7fff / (double)0x7fffff;
fftw_complex in[1152],out[1152];
double  amplitudeData[1152];
uint32_t durationTime = 0;
uint32_t currentTime = 0;
mad_stream mp3Stream;
mad_frame mp3Frame;
mad_synth mp3Synth;
mad_header mp3Header;
SDL_AudioSpec want;


struct ID3V2header
{
    char header[3];
    char ver;
    char revision;
    char flag;
    unsigned char size[4];
};


struct ID3v2frameheader
{
    char frameid[4];
    unsigned char size[4];
    char flags[2];
};

class Mp3File
{
public:
   Mp3File(const char* file);
   ~Mp3File();
   string GetArtist() const;
   string GetTitle() const;
   string GetAlbum() const;
   int GetCoverSize() const;
   unsigned char* GetCover() const;
   int GetAudioSize() const;
   unsigned char* GetAudioData() const;
private:
    void ReadMp3File(const char* file);
    string ReadFrameInfo(FILE* fp, int len);
    unsigned char* ReadCover(FILE* fp, int& len);
    string artist;
    string title;
    string album;
    int coverSize;
    unsigned char* cover;
    int audioSize;
    unsigned char* audio;
};

class SDL2Base
{
public:
    SDL2Base();
    ~SDL2Base();
    void EventHandle();
    void CreateTextures(string artist, string title, string album, unsigned char* buf, int bufLen);
private:
    SDL_Texture* CreateText(string text);
    SDL_Texture* CreateImage(unsigned char* buf, int len);
    void Draw();
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    bool quit;
    SDL_Event ev;
    SDL_Texture *tex[4] = {0};
    SDL_Point point[288];

};

void audio_play_callback(void *data, Uint8* stream, int len)
{
    memset(stream, 0,len);
    mad_frame_decode(&mp3Frame, &mp3Stream);
    mad_synth_frame(&mp3Synth, &mp3Frame);
    int16_t* temp = (int16_t*)malloc(len);
    int16_t* p = temp;
    for (int i = 0; i < 1152; ++i)
    {
        *p++ = (mp3Synth.pcm.samples[0][i] >> 7) * rate24_16*3;
        *p++ = (mp3Synth.pcm.samples[1][i] >> 7) * rate24_16*3;
    }
    SDL_MixAudio(stream, (uint8_t *) temp, len, SDL_MIX_MAXVOLUME);
    currentTime += 1152.0 / mp3Frame.header.samplerate * 1000;
    free(temp);
    for(int i=0; i<1152; ++i)
    {
        in[i][0]=(mp3Synth.pcm.samples[0][i] + mp3Synth.pcm.samples[1][i])>>1;
        in[i][1]=0;
    }
    fftw_plan plan=fftw_plan_dft_1d(1152,in,out,FFTW_FORWARD,FFTW_ESTIMATE);
    fftw_execute(plan);
    fftw_destroy_plan(plan);
    for(int i=0; i<1152; ++i)
    {
        amplitudeData[i]=(double)(out[i][0] / (1152 >> 1) / 0x7fffff);
    }
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        fprintf(stderr,"No input mp3 file!\n");
        exit(1);
    }

    setlocale(LC_ALL,"");
    Mp3File mp3Data(argv[1]);

    mad_stream_buffer(&mp3Stream, mp3Data.GetAudioData(), mp3Data.GetAudioSize());
    mad_header_decode(&mp3Header, &mp3Stream);
    durationTime = mp3Data.GetAudioSize() / ((mp3Header.bitrate * 144) / (double)mp3Header.samplerate) *
                    (1152.0 / mp3Header.samplerate * 1000);
    want.freq = mp3Header.samplerate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    #ifdef __unix
    want.samples = 1152 * 2;
    #endif // _unix
    #ifdef _WIN32
    want.samples = 1152;
    #endif // _WIN32
    want.callback = audio_play_callback;
    mad_header_finish(&mp3Header);
    SDL_OpenAudio(&want,NULL);
    int flag = 0;
    SDL_PauseAudio(flag);
    SDL2Base sdl2;
    sdl2.CreateTextures(mp3Data.GetArtist(), mp3Data.GetTitle(), mp3Data.GetAlbum(), mp3Data.GetCover(), mp3Data.GetCoverSize());
    sdl2.EventHandle();
    SDL_CloseAudio();
    mad_stream_finish(&mp3Stream);
    mad_frame_finish(&mp3Frame);
    mad_synth_finish(&mp3Synth);

}

Mp3File::Mp3File(const char* file)
{
    artist = "Unknown";
    title = "Unknown";
    album = "Unknown";
    coverSize = 0;
    audioSize = 0;
    cover = NULL;
    audio = NULL;
    ReadMp3File(file);
}

Mp3File::~Mp3File()
{
    if(audioSize > 0)
        free(audio);
    if(coverSize > 0)
        free(cover);
}

void Mp3File::ReadMp3File(const char* file)
{
    FILE* fp = fopen(file, "rb");
    if(fp == NULL)
    {
    fprintf(stderr, "Cannot open mp3 file: %s !\n",file);
    exit(1);
    }
    fseek(fp, 0, SEEK_END);
    audioSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ID3V2header v2head;
    ID3v2frameheader v2framehead;
    fread(&v2head, 1, 10, fp);
    if(strncmp(v2head.header, "ID3", 3) == 0)
    {
        int headlen = (v2head.size[0] & 0x7f) * 0x200000 + (v2head.size[1] & 0x7f) * 0x4000
                        + (v2head.size[2] & 0x7f) * 0x80 + (v2head.size[3] & 0x7f);
        int k = headlen;
        while(k > 0)
        {
            fread(&v2framehead, 1, 10, fp);
            int frameheadlen =  v2framehead.size[0] * 0x1000000 + v2framehead.size[1] * 0x10000
                                + v2framehead.size[2] * 0x100 + v2framehead.size[3];
            if(frameheadlen == 0) break;
            if(strncmp(v2framehead.frameid, "TIT2", 4) == 0 )
            {
                title = ReadFrameInfo(fp, frameheadlen);
            }
            else if(strncmp(v2framehead.frameid, "TALB", 4) == 0 )
            {
                album = ReadFrameInfo(fp, frameheadlen);
            }
            else if(strncmp(v2framehead.frameid, "TPE1", 4) == 0 )
            {
                artist = ReadFrameInfo(fp, frameheadlen);
            }
            else if(strncmp(v2framehead.frameid, "APIC", 4) == 0)
            {
                cover = ReadCover(fp, frameheadlen);
                coverSize = frameheadlen;
            }
            else
                fseek(fp, frameheadlen, SEEK_CUR);
            k -= frameheadlen;
        }
        fseek(fp, headlen + 10, SEEK_SET);
        audioSize -= headlen + 10;
        unsigned char a;
        while(1)
        {
            fread(&a, 1, 1, fp);
            audioSize --;
            if(a == 0xFF)
            {
                fread(&a, 1, 1, fp);
                if(a == 0xFB || a == 0xF3 || a == 0xF2 || a == 0xFA || a == 0xE3 || a == 0xE2 || a == 0xEB || a == 0xEA)
                {
                    audioSize ++;
                    fseek(fp, -2, SEEK_CUR);
                    break;
                }
                else
                    audioSize --;
            }

        }
    }
    else
        fseek(fp, -10, SEEK_CUR);
    audio = (unsigned char*)malloc(audioSize);
    if(audio == NULL)
    {
        fprintf(stderr,"Cannot malloc!\n");
        exit(1);
    }
    fread(audio, 1, audioSize, fp);
    fclose(fp);
}

string Mp3File::ReadFrameInfo(FILE *fp, int len)
{
    char buf[2048];
    char encode;
    fread(&encode, 1, 1, fp);
    if(encode == 1 || encode == 2)
    {
        wchar_t wbuf[1024];
        for(int i=0; i<(len-1)/2; i++)
            fread(&wbuf[i],1,2,fp);
        wbuf[(len-1)/2] = L'\0';
        wcstombs(buf, wbuf, 2048);
    }
    else
    {
        fread(buf, 1, len - 1, fp);
        buf[len-1] = '\0';
    }
    return string(buf);
}

unsigned char* Mp3File::ReadCover(FILE* fp, int& len)
{
    int k = 0;
    int t = len;
    len -= 11;
    fseek(fp, 11, SEEK_CUR);
    k += 11;
    unsigned char st;
    while(1)
    {
        fread(&st, 1, 1, fp);
        len -= 1;
        k += 1;
        if(st == 0xFF)
        {
            fread(&st, 1, 1, fp);
            len -= 1;
            k += 1;
            if(st == 0xD8)
                break;
        }
        if(st == 0x89)
        {
            fread(&st, 1, 1, fp);
            len -= 1;
            k += 1;
            if(st == 0x50)
                break;
        }
    }
    fseek(fp, -2, SEEK_CUR);
    unsigned char* cover = (unsigned char*)malloc(len);
    if(cover == NULL)
    {
        fprintf(stderr,"Cannot malloc!\n");
        exit(1);
    }
    fread(cover, 1, len, fp);
    k += len - 2;
    fseek(fp, t-k, SEEK_CUR);
    return cover;
}

unsigned char* Mp3File::GetAudioData() const
{
    return audio;
}

int Mp3File::GetAudioSize() const
{
    return audioSize;
}

string Mp3File::GetTitle() const
{
    return title;
}

string Mp3File::GetAlbum() const
{
    return album;
}

string Mp3File::GetArtist() const
{
    return artist;
}

unsigned char* Mp3File::GetCover() const
{
    return cover;
}

int Mp3File::GetCoverSize() const
{
    return coverSize;
}

SDL2Base::SDL2Base()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    TTF_Init();
    window = SDL_CreateWindow("mp3player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                600, 600, SDL_WINDOW_SHOWN);
    if(window == NULL)
    {
        fprintf(stderr, "Cannot create window!\n");
        exit(1);
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(renderer == NULL)
    {
        fprintf(stderr, "Cannot create renderer!\n");
        exit(1);
    }
   #ifdef __unix
    font = TTF_OpenFont("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 15);
   #endif // __unix
   #ifdef _WIN32
    font = TTF_OpenFont("C:\\Windows\\Fonts\\simhei.ttf", 15);
   #endif // _WIN32
    if(font == NULL)
    {
        fprintf(stderr, "Cannot open font!\n");
        exit(1);
    }
    quit = false;
}

SDL2Base::~SDL2Base()
{
    for(int i = 0;i < 4; i++)
        SDL_DestroyTexture(tex[i]);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    SDL_Quit();

}
void SDL2Base::EventHandle()
{
    double oldTime = SDL_GetTicks();
    double tpf;
    double tpfLimit = 1000.0/30;
    int flag = 0;
    while(!quit && currentTime < durationTime)
    {
        while(SDL_PollEvent(&ev))
        {
            if(ev.type == SDL_QUIT)
            {
                quit = true;
            }
            if(ev.type == SDL_KEYDOWN)
            {
                if(ev.key.keysym.sym == SDLK_SPACE)
                {
                    flag = !flag;
                    SDL_PauseAudio(flag);
                }
            }
        }
        Draw();
        tpf = SDL_GetTicks() - oldTime;
        if( tpf < tpfLimit)
            SDL_Delay((int)tpfLimit - tpf + 1);
        oldTime = SDL_GetTicks();
    }
}

SDL_Texture* SDL2Base::CreateText(string text)
{
    SDL_Color black = {0, 0, 0, 0xFF};
    SDL_Surface* temp = TTF_RenderUTF8_Blended(font, text.c_str(), black);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, temp);
    SDL_FreeSurface(temp);
    return tex;
}

SDL_Texture* SDL2Base::CreateImage(unsigned char* buf, int len)
{
    SDL_RWops *source = SDL_RWFromMem(buf, len);
    SDL_Surface *image = IMG_Load_RW(source, 1);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, image);
    SDL_FreeSurface(image);
    return tex;
}

void SDL2Base::CreateTextures(string artist, string title, string album, unsigned char* buf, int bufLen)
{

    tex[0] = CreateText("Artist:"+artist);
    tex[1] = CreateText("Title:"+title);
    tex[2] = CreateText("Album:"+album);
    tex[3] = CreateImage(buf, bufLen);
}

void SDL2Base::Draw()
{
    int w,h;
    char buf[200];
    sprintf(buf, "Time:%02d:%02d/%02d:%02d",(int)(currentTime / 1000.0) / 60,(int)(currentTime / 1000.0) % 60,
            (int)(durationTime / 1000.0) / 60,(int)(durationTime / 1000.0) % 60);
    SDL_Texture* time = CreateText(string(buf));
    SDL_QueryTexture(time, 0, 0, &w, &h);
    SDL_Rect timeRect = {220, 0, w, h};
    SDL_Rect rect[3];
    for(int i = 0;i < 3; i++)
    {
        SDL_QueryTexture(tex[i], 0, 0, &w, &h);
        rect[i].x = 50;
        rect[i].y = 80 + i * 60;
        rect[i].w = w;
        rect[i].h = h;
    }
    SDL_Rect imgRect = {350, 50, 200, 200};
    SDL_SetRenderDrawColor(renderer,255,255,255,0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer,255,0,0,0);
    SDL_RenderCopy(renderer, tex[3], NULL, &imgRect);
    SDL_RenderCopy(renderer, time, NULL, &timeRect);
    for(int i = 0;i < 3;i++)
        SDL_RenderCopy(renderer, tex[i], NULL, &rect[i]);

    for(int i = 0; i < 288; i++)
    {
        point[i].x = i * 2;
        point[i].y = 400 - 20 * amplitudeData[i];
    }

    SDL_RenderDrawLines(renderer, point, 288);
    SDL_RenderPresent(renderer);
    SDL_DestroyTexture(time);
}
