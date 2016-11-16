
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>

#include <zlib.h>

#include "psf/eng_protos.h"

#include "ao.h"

#include "conf.h"

#define DEBUG_MAIN (0)




#define TEXT_SCROLL_GAP 32

#define CONFIG_FNAME "ao.conf"


static int c_screen_w = 320;
static int c_screen_h = 240;

#define SCREENWIDTH c_screen_w
#define SCREENHEIGHT c_screen_h
#define SCOPEWIDTH c_screen_w

static int c_fps = 60;
#define FPS c_fps


static int col_text[3] = {50,50,50};
static int col_text_bg[3] = {230,230,250};
static int col_bg[3] = {255, 255, 255};
static int col_scope[3] = {0, 0, 0};
static int col_scope_low[3] = {255, 0, 0};
static int col_scope_high[3] = {0, 0, 255};

#define TEXT_COLOR col_text[0],col_text[1],col_text[2]
#define TEXT_BG_COLOR col_text_bg[0],col_text_bg[1],col_text_bg[2]
#define BG_COLOR col_bg[0],col_bg[1],col_bg[2]
#define SCOPE_COLOR col_scope[0],col_scope[1],col_scope[2]
#define SCOPE_LOW_COLOR col_scope_low[0],col_scope_low[1],col_scope_low[2]

static int c_scope_do_grad = 1;


static char c_ttf_font[256] = "ttf/gohufont-11.ttf";
#define TTF_FONT c_ttf_font

static int set_len_ms;


#define WIN_TITLE "Audio Overkill"

#define MAXCHAN 24


static int c_buf_size = (44100/30);
#define ABUFSIZ c_buf_size

static int f_borderless = 0;


static int fok=AO_FAIL;


static int samples=44100 / 30;

static int16_t * scope;
static int16_t scope_buf[1024];
static int scope_bufsiz = -1;
static int scope_clear = 1;
static int scope_dump_max = 1024;

static float scope_view = 0.5;

static int mono = 0;

static int channel_enable[MAXCHAN] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

static int u_size = 3000;
static Uint8  *u_buf;
static int u_buf_fill_start = 0; /* where sdl begins to fill audio stream from */
static int u_buf_fill = 0; /* size in bytes filled from fill_start and on.*/


#define STEREO_FILL_SIZE (c_buf_size*2)



static SDL_PixelFormat *pw_fmt;
static int pw_bpp, pw_pitch;
static uint32_t pw_ucol;
static uint8_t *pw_pixel;


static int f_render_font = 1;

int limint(int in, int from, int to);
char *str_prepend(char *s, char *pre);
char *get_fn_only(char *path);
int  pw_init(SDL_Surface *in);
void pw_set_rgb(int r,int g,int b);
void pw_fill(SDL_Surface *in);
void pw_set(SDL_Surface *in, int x, int y);
void load_conf(char * fn);
void dump_scope_buf();
int cheap_is_dir(char *path);
char *get_lib_dir(char *path);
void reset_u_buf();
void update(const Uint8 * buf, int size);
void fill_audio(void *udata, Uint8 *stream, int len);
int load_psf_file(char *fn);
void close_psf_file();
void free_scope();
void init_scope();
void clear_scope();
void update_scope(SDL_Surface *in);
void update_ao_chdisp(SDL_Surface *in);
void update_ao_ch_flag_disp(SDL_Surface *in, int md);
void render_text( SDL_Surface* dst, char *str,
	SDL_Color *col_a, SDL_Color *col_b,
	TTF_Font *font, int x, int y,
	int bold, int maxwidth, int tick);
void sdl_set_col(SDL_Color *c, int r,int  g,int  b,int  a);
void u_buf_init();

int get_file_type(char * fn);
void print_f_type(int type);

enum
{
	F_UNKNOWN,
	F_PSF,
	F_PSF2,
	F_USF,
	F_VGM,
};

enum
{
	M_PLAY,
	M_ERR,
	M_PAUSE,
	M_DO_STOP,
	M_DO_ERR_STOP,
	M_STOPPED,
	M_LOAD,
	M_RELOAD,
	M_RELOAD_IDLE
};

static play_stat = M_STOPPED;

static void (*file_close)(void);
static int (*file_execute)(void (*update )(const void *, int));
static int (*file_open)(char *);
	
int main(int argc, char *argv[])
{
	int run=1, i, fcnt, fcur,
		key, ispaused=0, newtrackcnt=20,
		tick, rtmpy;
	SDL_Event e;
	char *fn[1024];
	char tmpstr[256];
	char chtrack;
	
	/* point to psf functions only for now. */
	file_close = &close_psf_file;
	file_execute = &psf_execute;
	file_open = &load_psf_file;
	
	load_conf(CONFIG_FNAME);
	
	SDL_AudioSpec wanted;
	SDL_Window *win;
	SDL_Surface *screen;
	SDL_Color tcol_a;
	SDL_Color tcol_b;
	SDL_Surface* tsurf;
	SDL_Rect tr;
	
	FILE *fp;
	
	sdl_set_col(&tcol_a, TEXT_COLOR, 0);
	sdl_set_col(&tcol_b, TEXT_BG_COLOR, 0);
	
	tr.w=SCREENWIDTH;
	tr.h=SCREENHEIGHT;
	
	u_buf_init();
	
	init_scope();
	
	TTF_Init();
	
    TTF_Font *font = TTF_OpenFont(TTF_FONT, 11);
    
    if (!font)
    {
		fprintf(stderr,"err: could't find \"%s\".\n no text will be rendered.\n", TTF_FONT);
		f_render_font=0;
	}
		
	
	fcnt=0;
	for (i=1;i<argc && fcnt<1024;i++)
	{
		fn[fcnt] = argv[i];
		
		if (is_dir(fn[fcnt]))
		{
			fprintf(stderr, "opening directories not supported\n");
			return 1;
		}
		
		fcnt++;
	}
	
	fcur = 0;
	
	win = SDL_CreateWindow(WIN_TITLE,
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		SCREENWIDTH, SCREENHEIGHT,
		SDL_WINDOW_OPENGL | (SDL_WINDOW_BORDERLESS * f_borderless));
		
	screen = SDL_GetWindowSurface(win);
	
	set_channel_enable(channel_enable);
	
	
	
    /* set the audio format */
    wanted.freq = 44100;
    wanted.format = AUDIO_S16;
    wanted.channels = 2;    /* 1 = mono, 2 = stereo */
    wanted.samples = ABUFSIZ;
    wanted.callback = fill_audio;
    wanted.userdata = 0;
    

    /* open the audio device, forcing the desired format */
    if ( SDL_OpenAudio(&wanted, NULL) < 0 )
    {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return 0;
    }
    
    
    SDL_PauseAudio(1);
    
    play_stat = M_LOAD;
    
    
    ispaused=0;
    chtrack=0;

	tick=0;
	
	while (run)
	{
		while ( SDL_PollEvent( &e ) )
		{
			switch ( e.type )
			{
			case SDL_QUIT:
				run = 0;
				break;
			case SDL_KEYDOWN:
				
				switch ( e.key.keysym.sym )
				{
				/*case SDLK_q:*/
				case SDLK_ESCAPE:
					run = 0;
					break;
					
				case SDLK_SPACE:
					if (play_stat == M_STOPPED || play_stat == M_ERR)
						play_stat = M_LOAD;
					else if (play_stat == M_PAUSE)
						play_stat = M_PLAY;
					else if (play_stat == M_PLAY)
						play_stat = M_PAUSE;
				
					break;
				
				case SDLK_m:
					if (mono==1)
						mono=0;
					else
						mono=1;
					break;
				
				case SDLK_LEFT:
					if (fcur>0) fcur--;
					
					if (play_stat == M_PLAY)
						play_stat = M_RELOAD;
					else if (play_stat == M_PAUSE)
						play_stat = M_RELOAD_IDLE;
					
					if (play_stat == M_STOPPED || play_stat == M_ERR)
						play_stat = M_LOAD;
					
					break;
				
				case SDLK_RIGHT:
					if (fcur<(fcnt-1)) fcur++;
					
					if (play_stat == M_PLAY)
						play_stat = M_RELOAD;
					else if (play_stat == M_PAUSE)
						play_stat = M_RELOAD_IDLE;
					
					if (play_stat == M_STOPPED || play_stat == M_ERR)
						play_stat = M_LOAD;
					
					break;
				
				case SDLK_0:
					if (ao_sample_limit[0]==0)
						ao_sample_limit[0]=1;
					else
						ao_sample_limit[0]=0;
					break;
				case SDLK_1:
					ao_sample_do[0] = ao_sample_do[0] ? 0 : 1;
					break;
				case SDLK_2:
					ao_sample_do[1] = ao_sample_do[1] ? 0 : 1;
					break;
				case SDLK_3:
					ao_sample_do[2] = ao_sample_do[2] ? 0 : 1;
					break;
				case SDLK_4:
					ao_sample_do[3] = ao_sample_do[3] ? 0 : 1;
					break;
				case SDLK_5:
					ao_sample_do[4] = ao_sample_do[4] ? 0 : 1;
					break;
				case SDLK_6:
					ao_sample_do[5] = ao_sample_do[5] ? 0 : 1;
					break;
				case SDLK_7:
					ao_sample_do[6] = ao_sample_do[6] ? 0 : 1;
					break;
				case SDLK_8:
					ao_sample_do[7] = ao_sample_do[7] ? 0 : 1;
					break;
				case SDLK_9:
					ao_sample_do[8] = ao_sample_do[8] ? 0 : 1;
					break;
				case SDLK_q:
					ao_sample_do[9] = ao_sample_do[9] ? 0 : 1;
					break;
				case SDLK_w:
					ao_sample_do[10] = ao_sample_do[10] ? 0 : 1;
					break;
				case SDLK_e:
					ao_sample_do[11] = ao_sample_do[11] ? 0 : 1;
					break;
				case SDLK_r:
					ao_sample_do[12] = ao_sample_do[12] ? 0 : 1;
					break;
				case SDLK_t:
					ao_sample_do[13] = ao_sample_do[13] ? 0 : 1;
					break;
				case SDLK_y:
					ao_sample_do[14] = ao_sample_do[14] ? 0 : 1;
					break;
				case SDLK_u:
					ao_sample_do[15] = ao_sample_do[15] ? 0 : 1;
					break;
				case SDLK_i:
					ao_sample_do[16] = ao_sample_do[16] ? 0 : 1;
					break;
				case SDLK_o:
					ao_sample_do[17] = ao_sample_do[17] ? 0 : 1;
					break;
				case SDLK_p:
					ao_sample_do[18] = ao_sample_do[18] ? 0 : 1;
					break;
				
				case SDLK_c:
					load_conf(CONFIG_FNAME);
					sdl_set_col(&tcol_a, TEXT_COLOR, 0);
					sdl_set_col(&tcol_b, TEXT_BG_COLOR, 0);
					free_scope();
					init_scope();
					break;
				
				default:
					break;
				}
				break;
			default:
				break;
			}
		}
		
		
		SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, BG_COLOR));
		
		
		if (play_stat == M_PLAY)
			dump_scope_buf();
			
		update_scope(screen);
		
		update_ao_chdisp(screen);
		update_ao_ch_flag_disp(screen, 2);
		
		rtmpy = 8;
		
		if (get_corlett_title() != 0)
		{
			
			render_text(
				screen,
				get_corlett_title(),
				&tcol_a,
				&tcol_b,
				font,
				10,rtmpy,
				1,SCREENWIDTH/2,tick);
			
			rtmpy += 12;
			
			render_text(
				screen,
				get_corlett_artist(),
				&tcol_a,
				&tcol_b,
				font,
				10,rtmpy,
				1,SCREENWIDTH/2,tick);
			
			rtmpy += 12;
				
			render_text(
				screen,
				get_corlett_game(),
				&tcol_a,
				&tcol_b,
				font,
				10,rtmpy,
				1,SCREENWIDTH/2,tick);
			
			rtmpy += 12;
			
				
		}
		else
			rtmpy += 12*3;
		
		switch (play_stat)
		{
		case M_PAUSE:
			render_text(
				screen, "- paused -", &tcol_a, &tcol_b,
				font, 10,SCREENHEIGHT - 16,1 ,SCREENWIDTH/2 ,tick);
			break;
		case M_STOPPED:
			render_text(
				screen, "- stopped -", &tcol_a, &tcol_b,
				font, 10,SCREENHEIGHT - 16,1 ,SCREENWIDTH/2 ,tick);
			break;
		case M_ERR:
			render_text(
				screen, "- error -", &tcol_a, &tcol_b,
				font, 10,SCREENHEIGHT - 16,1 ,SCREENWIDTH/2 ,tick);
			break;
		default:break;
		}
		
		
		if (mono)
			render_text(
				screen, "mono", &tcol_a, &tcol_b,
				font, SCREENWIDTH-64, 8,  1,SCREENWIDTH/2,tick);
		else
			render_text(
				screen, "stereo", &tcol_a, &tcol_b,
				font, SCREENWIDTH-64, 8,  1,SCREENWIDTH/2,tick);
				
		
		switch (play_stat)
		{
		case M_DO_STOP:
		case M_DO_ERR_STOP:
			SDL_PauseAudio(1);
			close_psf_file();
			break;
		
		case M_RELOAD_IDLE:
		case M_RELOAD:
			SDL_PauseAudio(1);
			
			reset_u_buf();
		
			file_close();
			
		case M_LOAD:
			SDL_PauseAudio(1);
			
			reset_u_buf();
			
			if (get_file_type(fn[fcur]) != F_PSF)
			{
				play_stat = M_ERR;
				break;
			}
		
			file_open(fn[fcur]);
			
			clear_scope();
			
			update_scope(screen);
			
			newtrackcnt=20;
			tick=0;
			
			if (play_stat == M_RELOAD_IDLE)
				play_stat = M_PAUSE;
			else if (play_stat != M_ERR)
				play_stat = M_PLAY;
			
			break;
			
		case M_PLAY:
			SDL_PauseAudio(0);
			break;
			
		case M_PAUSE:
			SDL_PauseAudio(1);
			break;
		}
		
		
		sprintf(tmpstr,"(%d/%d)",fcur+1,fcnt);
		
		render_text(
			screen,
			tmpstr,
			&tcol_a,
			&tcol_b,
			font,
			10, rtmpy,
			1,SCREENWIDTH/2,tick);
		
		
		set_channel_enable(channel_enable);
		
		SDL_UpdateWindowSurface(win);
		
		
        SDL_Delay(1000/FPS);
        
        if (newtrackcnt<0)
			tick++;
        
        newtrackcnt--;
	
	}
	
	SDL_CloseAudio();
	close_psf_file();
	free_scope();
	
}

int pw_init(SDL_Surface *in)
{
	
	pw_fmt=in->format;
	pw_bpp = pw_fmt->BytesPerPixel;
	pw_pitch = in->pitch;
	
	return pw_bpp==4;
		
}

void pw_set_rgb(int r,int g,int b)
{
	
	pw_ucol = SDL_MapRGB(pw_fmt, r,g,b);
}

void pw_fill(SDL_Surface *in)
{
	SDL_FillRect(in, 0, pw_ucol);
}

void pw_set(SDL_Surface *in, int x, int y)
{
	pw_pixel = (uint8_t*) in->pixels;
	pw_pixel += (y * pw_pitch) + (x * pw_bpp);
	*((uint32_t*)pw_pixel) = pw_ucol;
}

int limint(int in, int from, int to)
{
	if (in < from)
		in = from;
	if (in > to)
		in = to;
	return in;
}


void load_conf(char * fn)
{
	struct cfg_entry *o, *tmp;
	
	o = read_conf(fn);
	
	if (!o)
		return;
	
	tmp = o;
	
	#if DEBUG_MAIN
	print_cfg_entries(o);
	#endif
	
	while (tmp)
	{
		if (strcmp("general",tmp->section)==0)
		{
			
			if (strcmp("fps",tmp->name) == 0  && tmp->type==E_INT)
			{
				if (tmp->dat[0].i > 0)
					c_fps = limint(tmp->dat[0].i, 1, 128);
			}
			else if (strcmp("ttf_font",tmp->name) == 0 && tmp->type==E_STR  )
			{
				strcpy(c_ttf_font, tmp->dat[0].s);
			}
			else if (strcmp("text_color",tmp->name) == 0  && tmp->type==E_RGB )
			{
				col_text[0] = limint(tmp->dat[0].i, 0, 255);
				col_text[1] = limint(tmp->dat[1].i, 0, 255);
				col_text[2] = limint(tmp->dat[2].i, 0, 255);
			}
			
			else if (strcmp("text_bg_color",tmp->name) == 0 && tmp->type==E_RGB )
			{
				col_text_bg[0] = limint(tmp->dat[0].i, 0, 255);
				col_text_bg[1] = limint(tmp->dat[1].i, 0, 255);
				col_text_bg[2] = limint(tmp->dat[2].i, 0, 255);
			}
			
			else if (strcmp("bg_color",tmp->name) == 0 && tmp->type==E_RGB )
			{
				col_bg[0] = limint(tmp->dat[0].i, 0, 255);
				col_bg[1] = limint(tmp->dat[1].i, 0, 255);
				col_bg[2] = limint(tmp->dat[2].i, 0, 255);
			}
			else if (strcmp("scope_color",tmp->name) == 0 && tmp->type==E_RGB)
			{
				col_scope[0] = limint(tmp->dat[0].i, 0, 255);
				col_scope[1] = limint(tmp->dat[1].i, 0, 255);
				col_scope[2] = limint(tmp->dat[2].i, 0, 255);
			}
			else if (strcmp("scope_low_grad_color",tmp->name) == 0 && tmp->type==E_RGB)
			{
				col_scope_low[0] = limint(tmp->dat[0].i, 0, 255);
				col_scope_low[1] = limint(tmp->dat[1].i, 0, 255);
				col_scope_low[2] = limint(tmp->dat[2].i, 0, 255);
			}
			else if (strcmp("scope_high_grad_color",tmp->name) == 0 && tmp->type==E_RGB)
			{
				col_scope_high[0] = limint(tmp->dat[0].i, 0, 255);
				col_scope_high[1] = limint(tmp->dat[1].i, 0, 255);
				col_scope_high[2] = limint(tmp->dat[2].i, 0, 255);
			}
			else if (strcmp("buf_size", tmp->name)==0 && tmp->type==E_INT)
			{
				c_buf_size = limint(tmp->dat[0].i, 16, 4096*4);
			}
			else if (strcmp("noborder", tmp->name)==0 && (tmp->type==E_BOOL || tmp->type==E_INT))
			{
				f_borderless = tmp->dat[0].i;
			}
			else if (strcmp("len_ms", tmp->name)==0 && tmp->type==E_INT)
			{
				set_len_ms = tmp->dat[0].i;
			}
			else if (strcmp("scope_grad", tmp->name)==0 && (tmp->type==E_BOOL || tmp->type==E_INT))
			{
				c_scope_do_grad = tmp->dat[0].i;
			}
			else if (strcmp("screen_width", tmp->name)==0 && tmp->type==E_INT)
			{
				c_screen_w = limint(tmp->dat[0].i, 16, 4096*2);
			}
			else if (strcmp("screen_height", tmp->name)==0 && tmp->type==E_INT)
			{
				c_screen_h = limint(tmp->dat[0].i, 16, 4096*2);
			}
		}
		
		
		tmp = tmp->next;
	}
	
	if (o!=0)
		free_cfg_entries(o);
}

void dump_scope_buf()
{
	int i, j;
	float bufj;
	
	if (scope_bufsiz<=0)
	{
		for (i=0;i<SCOPEWIDTH;i++)
			scope[i] = 0;
	}
	else
	{
		if (scope_bufsiz>scope_dump_max)
			scope_bufsiz = scope_dump_max;
			
		bufj = (float) scope_bufsiz / SCOPEWIDTH;
		
		bufj *= scope_view;
		
		for (i=0;i<SCOPEWIDTH;i++)
		{
			j = bufj*i;
			
			j = j < 0 ? 0 : j;
			j = j >= scope_bufsiz ? scope_bufsiz-1 : j;
			
			scope[i] = scope_buf[j];
		}
	}
	
	
	scope_bufsiz = 0;
	return;
}


char *str_prepend(char *s, char *pre)
{
	if (!s || !pre)
		return 0;
	
	char * nstr;
	int i,j;
	
	nstr = (char*) malloc( strlen(s)+ strlen(pre) + 1);
	
	sprintf(nstr, "%s%s",pre,s);
	
	
	return nstr;
}

char *get_fn_only(char *path)
{
	char *t = &path[0];
	char *f = 0;
	
	while (*t!='\0')
	{
		if (*(t-1)=='\\' || *(t-1)=='/')
			f=t;
		
		t++;
	}
	
	return f;
}

int cheap_is_dir(char *path)
{
	int l;
	
	if (path==0)
		return -1;
	
	if (path[strlen(path)-1]=='/' || path[strlen(path)-1]=='\\')
		return 1;
		
	return 0;
		
	
	
}

char *get_lib_dir(char *path)
{
	int i;
	char *slash, *npath, *ch;
	
	if (!path)
		return 0;
		
	slash = strrchr(path, '/');
	
	if (!slash)
		return 0;
		
	npath = (char *) malloc(strlen(path));
	
	ch = path;
	i=0;
	while ((ch) <= slash)
	{
		npath[i] = path[i];
		
		ch++;
		i++;
	}
	
	npath[i] = '\0';	
	
	return npath;
	
}

void reset_u_buf()
{
	int i;
	
	u_buf_fill_start = 0;
	u_buf_fill = 0;
	
	for (i=0;i<u_size;i++)
		u_buf[i] = 0;
}

void update(const Uint8 * buf, int size)
{
	/* as the buffer execute_psf fills update with is always
	 * fixed, we have another buffer to allow different buffer sizes.
	 */
	int i,j, tmp, tmp1, goal, over,newsz,got;
	signed short stmp, stmp1;
	Uint8 *ctmp;
	
	if (buf==0 || size==0)
	{
		fok=0; /* if function calling update returns 0, 0, halt playback.
				* 
				* (WATCH THIS)
				*/
		play_stat = M_DO_STOP;
		
		return;
	}
	
	/* resize u_buf if update thows too much data */
	if (size > (u_size-(u_buf_fill*2)))
	{
		newsz = (size * 2 + (size/2)) + (u_buf_fill*2);
		
		#if DEBUG_MAIN
		printf("note: buffer resize from %d to %d\n", (u_size), newsz);
		#endif
		ctmp = ( Uint8 *) malloc(sizeof ( Uint8 ) * newsz);
		
		
		j=u_buf_fill_start;
		i=0;
		goal = (u_buf_fill + u_buf_fill_start) % u_size;
		while (j!=goal)
		{
			ctmp[i] = u_buf[j];
			
			j++;
			j%=u_size;
			
			i++;
		}
		
		free(u_buf);
		u_buf = ctmp;
		ctmp=0;
		u_size = newsz;
		u_buf_fill_start = 0;
	}
	
	
	j=u_buf_fill_start;
	i=0;
	goal = u_buf_fill_start + (STEREO_FILL_SIZE);
	goal %= u_size;
	
	
	j+=u_buf_fill;
	j%=u_size;
		
	
	if (mono==0) /* STREREO */
		while (i < size)
		{
			u_buf[j] = buf[i];
			
			u_buf_fill++;
				
			
			j++;
			j%=u_size;
			
			i++;
		}
	else /* MONO  (assuming 4-byte 2ch signed short le )*/
		while (i < size)
		{
			if (i%4==0)
			{
				stmp = (buf[i+1]<<8) + (buf[i] & 0xff);
				stmp1 = (buf[i+3]<<8) + (buf[i+2] & 0xff);
				
				tmp = ((int)stmp + (int)stmp1) / 2;
				
				u_buf[j % u_size] = tmp & 0xff;
				u_buf[j+1 % u_size] = (tmp>>8) & 0xff;
				
				u_buf[j+2 % u_size] = tmp & 0xff;
				u_buf[j+3 % u_size] = (tmp>>8) & 0xff;
				
			
				u_buf_fill+=4;
					
				
				j+=4;
				j%=u_size;
				
			}
			
			i++;
		}
		
		
	
	return;
}

void fill_audio(void *udata, Uint8 *stream, int len)
{
	int i, j;
	
	if (play_stat==M_PLAY)
	{
		while (u_buf_fill < len && play_stat == M_PLAY)
		{
			fok = file_execute(update);
			
			if (fok==AO_FAIL)
				play_stat = M_DO_STOP;
		}
			
			/* here, 'psf_execute' should be replaced by a dereferenced
			 * function pointer.
			 * int32_t func(void (*update)(const void *, int))
			 */
		
		
		if (play_stat != M_PLAY)
		{
			reset_u_buf();
			return;
		}
		
		i=0;
		j=u_buf_fill_start;
		
		while (i < len)
		{
			stream[i] = u_buf[j]; /* fill stream */
			
			
			i++;
			
			if (j%4==0 && j+1 < u_size && scope_bufsiz < 1024)
			{
				scope_buf[scope_bufsiz] = (u_buf[j+1]<<8) + (u_buf[j]);
				scope_bufsiz++;
			}
			
			j++;
			j%=u_size;
			
			u_buf_fill--;
		}
		
		u_buf_fill_start += len;
		u_buf_fill_start %= u_size;
		
			
	}
	
}

int load_psf_file(char *fn)
{
	char * ctmp;
	struct filebuf *fb = filebuf_init();
	int tlen;
	FILE *fp;
	
	if (play_stat == M_PLAY)
		return 1;
		
	ao_set_len = set_len_ms;
	
	u_int8_t *tbuf;
	#if DEBUG_MAIN
	printf("prep?\n");
	#endif
	if (fn != 0)
	{
		ctmp=get_lib_dir(fn);
		#if DEBUG_MAIN
		printf("get_lib_dir ok\n");
		printf("str_prepend ok\n");
		#endif
		set_libdir(ctmp);
		#if DEBUG_MAIN
		printf("set_libdir ok\n");
		#endif
	}
	#if DEBUG_MAIN
	else
		printf("no libdir?\n");
	printf("ok\n");
	#endif
	
	filebuf_load(fn, fb);
	
	if (fb->len==0)
	{
		play_stat = M_ERR;
		
		#if DEBUG_MAIN
		printf("load failed\n");
		#endif
		
		return 0;
	}
		
	
	fok = psf_start(fb->buf, fb->len);
	
	if (fok == AO_FAIL)
		play_stat = M_ERR;
	
	filebuf_free(fb);
	
	
	
	
	return fok;
}

void close_psf_file()
{
	if (  play_stat==M_PLAY ||
		  play_stat==M_DO_STOP || 
		  play_stat==M_DO_ERR_STOP ||
		  play_stat==M_RELOAD ||
		  play_stat==M_RELOAD_IDLE)
		psf_stop();
	
	if (play_stat==M_DO_ERR_STOP)
		play_stat=M_ERR;
	else if (play_stat != M_RELOAD && play_stat != M_RELOAD_IDLE)
		play_stat=M_STOPPED;
}

void free_scope()
{
	free(scope);
}

void init_scope()
{
	scope = malloc(sizeof(int16_t) * SCOPEWIDTH);
}

void clear_scope()
{
	int i;
	
	for (i=0;i<SCOPEWIDTH;i++)
		scope[i] = 0;
		
	for (i=0;i<scope_bufsiz;i++)
		scope_buf[i]=0;
}


void update_scope(SDL_Surface *in)
{
	
	int i, x, y;
	
	float fy, gfy;
	
	static int grgb[3];
	
	if (pw_init(in))
	{
		pw_set_rgb(BG_COLOR);
		
		pw_fill(in);
		
		pw_set_rgb(col_scope[0], col_scope[1], col_scope[2]);
		
		for (i=0;i<SCOPEWIDTH && i < SCREENWIDTH;i++)
		{
			
			x=i;
			
			fy=(float) scope[i] / (0xffff/2);

			if (c_scope_do_grad)
			{
				if (fy<0) /* use low gradient color */
				{
					gfy = -fy * 1.5;
					grgb[0] = col_scope[0] + (int) ((float) (col_scope_low[0]-col_scope[0]) * gfy);
					grgb[1] = col_scope[1] + (int) ((float) (col_scope_low[1]-col_scope[1]) * gfy);
					grgb[2] = col_scope[2] + (int) ((float) (col_scope_low[2]-col_scope[2]) * gfy);
				}
				else /* use high gradient color */
				{
					gfy = fy * 1.5;
					grgb[0] = col_scope[0] + (int) ((float) (col_scope_high[0]-col_scope[0]) * gfy);
					grgb[1] = col_scope[1] + (int) ((float) (col_scope_high[1]-col_scope[1]) * gfy);
					grgb[2] = col_scope[2] + (int) ((float) (col_scope_high[2]-col_scope[2]) * gfy);
				}
					
					
				
				
				grgb[0] = limint(grgb[0], 0, 255);
				grgb[1] = limint(grgb[1], 0, 255);
				grgb[2] = limint(grgb[2], 0, 255);
				
				pw_set_rgb(grgb[0], grgb[1], grgb[2]);
			}
			
			y = (int) (fy * (SCREENHEIGHT/2));
			y += (SCREENHEIGHT/2);
			y = y<0? 0:y;
			y = y>(SCREENHEIGHT-1)?(SCREENHEIGHT-1):y;
			
			pw_set(in, x, y);
			
			pw_set(in, x, y+1);
		}
	}
	
}

void update_ao_chdisp(SDL_Surface *in)
{
	
	int i, x, y, to_y;
	
	float fy;
	
	
	if (pw_init(in))
	{		
		pw_set_rgb(SCOPE_COLOR);
		
		for (i=0;i<(24*2);i++)
		{
			fy=(float) ( ao_chan_disp[i] ) / (0xfff);
			
			fy=fy>1.0?1.0:fy;
			fy=fy<0.0?0.0:fy;
			
			
			x = SCREENWIDTH - 10 - (24*2*2) +  ((i) + (i/2 * 2)  );
			y = SCREENHEIGHT - 10;
			to_y = y - (int) (fy*16);
			
			y+=1;
			
			if (ao_channel_enable[i/2] == 0)
				to_y = y + 3;
							
			while (y > to_y)
			{
				pw_set(in, x, y);
				y--;
			}
		
			while (y < to_y)
			{
				pw_set(in, x, y);
				y++;
			}
		}
	}
	
}

void update_ao_ch_flag_disp(SDL_Surface *in, int md)
{
	
	int i, j, x, y, tmp;
	
	float fy;
	
	static char tmpstr[32];
	
	SDL_Color tcol_a;
	
	sdl_set_col(&tcol_a, SCOPE_COLOR, 0);
	
	
	if (pw_init(in))
	{		
		pw_set_rgb(SCOPE_COLOR);
		
		for (i=0;i<(24);i++)
		{
			tmp = ao_chan_flag_disp[i];
			y=SCREENHEIGHT - (24*2) - 4 + (i*2);
			
			
			y = SCREENHEIGHT - 10;
			x = SCREENWIDTH - 10 - (24*2*2) +  (i + (i * 3))  ;
			
			
			if (md==1)
			{
				if (tmp & (1))
					pw_set(in, x, y+4);
				if (tmp & (2))
					pw_set(in, x, y+5);
				if (tmp & (4))
					pw_set(in, x, y+6);
			}
			
			
			if (md==2)
			{
				pw_set_rgb(tmp&0xff,tmp>>8&0xff,tmp>>16&0xff);
				pw_set(in, x, y+4);
				pw_set(in, x+1, y+4);
				pw_set(in, x, y+5);
				pw_set(in, x+1, y+5);
			}
			
			pw_set_rgb(SCOPE_COLOR);
				
			/*printf("%d\n",tmp);*/
		}
	}
	
}

void render_text(
	SDL_Surface* dst,
	char *str,
	SDL_Color *col_a,
	SDL_Color *col_b,
	TTF_Font *font,
	int x,
	int y,
	int bold,
	int maxwidth,
	int tick)
{
	static SDL_Surface* text;
	int maxtks, tkframe, shiftx, toobig=0;
	SDL_Rect tclip,tr;
	tclip.x=0;
	tclip.y=0;
	tclip.h=256;
	
	if (!f_render_font)
		return;
	
	
	tr.w=SCREENWIDTH;
	tr.h=SCREENHEIGHT;
	
	text = TTF_RenderText_Solid(font, str, *col_b);
	
	
	tclip.h=text->h;
	tclip.w=maxwidth;
	
	if (maxwidth < text->w)
	{
		maxtks=((text->w)+TEXT_SCROLL_GAP)*2;
		tkframe = tick % maxtks;
		
		shiftx = (tkframe/2);
		
		toobig=1;
	}
	
	
	if (toobig)
		tclip.x += shiftx;
	
	tr.x=x-1;
	tr.y=y;
	SDL_BlitSurface(text, &tclip, dst, &tr);
	SDL_FreeSurface(text);
	
	text = TTF_RenderText_Solid(font, str, *col_a);
	tr.x=x;
	SDL_BlitSurface(text, &tclip, dst, &tr);
	
	
	if (bold==1)
	{
		tr.x+=1;
		SDL_BlitSurface(text, &tclip, dst, &tr);
	}
	
	SDL_FreeSurface(text);
	
	if (toobig)
	{
		tclip.x=0;
		x+=(maxtks/2)-shiftx;
		tclip.w-=(maxtks/2)-shiftx;
		
		tr.x=x-1;
		text = TTF_RenderText_Solid(font, str, *col_b);
		SDL_BlitSurface(text, &tclip, dst, &tr);
		SDL_FreeSurface(text);
		
		text = TTF_RenderText_Solid(font, str, *col_a);
		tr.x=x;
		SDL_BlitSurface(text, &tclip, dst, &tr);
		
		if (bold==1)
		{
			tr.x+=1;
			SDL_BlitSurface(text, &tclip, dst, &tr);
		}
		
		SDL_FreeSurface(text);
	}
}

void sdl_set_col(SDL_Color *c, int r,int  g,int  b,int  a)
{
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
}

void u_buf_init()
{
	u_buf = ( Uint8 *) malloc(sizeof ( Uint8 ) * u_size);
}

char * get_ext(char * fn)
{
	char * e;
	
	e = (fn + strlen(fn) - 1);
	
	while (e > fn && *e != '.')
		e--;
		
	if (*e=='.')
		e++;
	
	return e;
}



int get_file_type(char * fn)
{
	FILE *fp;
	gzFile *gz_fp;
	char *ext, ch, i, is;
	
	if (!fn)
		return -1;
	
	ext = get_ext(fn);
	
	fp = fopen(fn, "r");
	
	if (!fp)
		return -1;
	
	fclose(fp);
		
		
	if (strcmp(ext,"psf") == 0 || strcmp(ext,"PSF") == 0)
		return F_PSF;
	if (strcmp(ext,"psf2") == 0 || strcmp(ext,"PSF2") == 0)
		return F_PSF2;
	if (strcmp(ext,"usf") == 0 || strcmp(ext,"USF") == 0)
		return F_USF;
	if (strcmp(ext,"vgm") == 0 || strcmp(ext,"VGM") == 0)
		return F_VGM;
	
	return F_UNKNOWN;
	
	/* psf ? 
	is=1;
	i=0;
	while ((ch=getc()) != EOF && is)
	{
		switch(i)
		{
		case 0:
			if (ch!='P')
				is=0;
			break;
		case 1:
			if (ch!='S')
				is=0;
			break;
		case 2:
			if (ch!='F')
				is=0;
			break;
		}
		i++;
	}
	
	if (is)
	{
		
	}
		
	rewind(fp);
	
	/* vgm ? */
}

void print_f_type(int type)
{
	switch (type)
	{
	case F_PSF:
		printf("psf\n");
		return;
	case F_PSF2:
		printf("psf2\n");
		return;
	case F_USF:
		printf("usf\n");
		return;
	case F_VGM:
		printf("vgm\n");
		return;
	default:
		printf("unknown\n");
	}
}

/*
	F_UNKNOWN,
	F_PSF,
	F_PSF2,
	F_USF,
	F_VGM,

*/
