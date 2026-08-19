#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <tinyalsa/asoundlib.h>

typedef struct { double b0,b1,b2,a1,a2,x1,x2,y1,y2; } Biquad;

static void lp_coeff(Biquad *q, double fc, double fs) {
    double w=2.0*M_PI*fc/fs, cw=cos(w), sw=sin(w), Q=0.7071067811865476;
    double alpha=sw/(2.0*Q), a0=1.0+alpha;
    q->b0=((1.0-cw)/2.0)/a0; q->b1=(1.0-cw)/a0; q->b2=((1.0-cw)/2.0)/a0;
    q->a1=(-2.0*cw)/a0; q->a2=(1.0-alpha)/a0;
}
static void hp_coeff(Biquad *q, double fc, double fs) {
    double w=2.0*M_PI*fc/fs, cw=cos(w), sw=sin(w), Q=0.7071067811865476;
    double alpha=sw/(2.0*Q), a0=1.0+alpha;
    q->b0=((1.0+cw)/2.0)/a0; q->b1=-(1.0+cw)/a0; q->b2=((1.0+cw)/2.0)/a0;
    q->a1=(-2.0*cw)/a0; q->a2=(1.0-alpha)/a0;
}
static double run_bq(Biquad *q, double x) {
    double y=q->b0*x + q->b1*q->x1 + q->b2*q->x2 - q->a1*q->y1 - q->a2*q->y2;
    q->x2=q->x1; q->x1=x; q->y2=q->y1; q->y1=y; return y;
}
typedef struct { Biquad bq[4]; int n; double gain; } Path;
static double run_path(Path *p, double x) { for(int i=0;i<p->n;i++) x=run_bq(&p->bq[i],x); return x*p->gain; }
static void path_lp(Path *p,double fc,double fs,double gain){p->n=2;lp_coeff(&p->bq[0],fc,fs);lp_coeff(&p->bq[1],fc,fs);p->gain=gain;}
static void path_hp(Path *p,double fc,double fs,double gain){p->n=2;hp_coeff(&p->bq[0],fc,fs);hp_coeff(&p->bq[1],fc,fs);p->gain=gain;}
static void path_bp(Path *p,double flo,double fhi,double fs,double gain){p->n=4;hp_coeff(&p->bq[0],flo,fs);hp_coeff(&p->bq[1],flo,fs);lp_coeff(&p->bq[2],fhi,fs);lp_coeff(&p->bq[3],fhi,fs);p->gain=gain;}

static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t rd32(const uint8_t *p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static int16_t clamp16(double v){ if(v>32767)v=32767; if(v<-32768)v=-32768; return (int16_t)lrint(v); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr,"usage: %s stereo.wav [card] [device]\n", argv[0]); return 1; }
    FILE *f=fopen(argv[1],"rb"); if(!f){perror("wav");return 1;}
    uint8_t hdr[12]; if(fread(hdr,1,12,f)!=12) return 1;
    uint16_t ch=0,bits=0; uint32_t rate=0; int data_off=0;
    uint8_t b[8];
    while(fread(b,1,8,f)==8){
        uint32_t id=rd32(b), sz=rd32(b+4);
        if(id==0x20746d66){
            uint8_t fmt[40]; size_t n=sz<40?sz:40; if(fread(fmt,1,n,f)!=(size_t)n) return 1;
            ch=rd16(fmt+2); rate=rd32(fmt+4); bits=rd16(fmt+14);
            if(ch!=2||bits!=16){fprintf(stderr,"need 2ch 16bit\n");return 1;}
            if(sz>40) fseek(f,sz-40,SEEK_CUR);
        } else if(id==0x61746164){ data_off=1; break; }
        else fseek(f,sz,SEEK_CUR);
    }
    if(!data_off||!rate) return 1;
    int card=0,device=24; if(argc>2) card=atoi(argv[2]); if(argc>3) device=atoi(argv[3]);
    int frames=0;
    struct pcm_config cfg; memset(&cfg,0,sizeof(cfg));
    cfg.channels=8; cfg.rate=rate; cfg.format=PCM_FORMAT_S16_LE;
    cfg.period_size=256; cfg.period_count=4;
    cfg.start_threshold=0; cfg.stop_threshold=0; cfg.silence_threshold=0;
    struct pcm *pcm=pcm_open(card,device,PCM_OUT,&cfg);
    if(!pcm||!pcm_is_ready(pcm)){fprintf(stderr,"pcm open failed: %s\n",pcm_get_error(pcm));return 1;}
    frames=(int)pcm_get_buffer_size(pcm);
    fprintf(stderr,"pcm ok: %u ch %u Hz buf=%d fr\n",cfg.channels,cfg.rate,frames);

    double fs=rate;
    Path L_low,R_low,L_mid,R_mid,L_high,R_high;
    path_lp(&L_low,250,fs,0.9); path_lp(&R_low,250,fs,0.9);
    path_bp(&L_mid,250,4000,fs,0.9); path_bp(&R_mid,250,4000,fs,0.9);
    path_hp(&L_high,4000,fs,0.9); path_hp(&R_high,4000,fs,0.9);

    int16_t *inbuf=malloc((size_t)frames*2*sizeof(int16_t));
    int16_t *outbuf=malloc((size_t)frames*8*sizeof(int16_t));
    if(!inbuf||!outbuf) return 1;
    size_t total=0;
    for(;;){
        size_t n=fread(inbuf,sizeof(int16_t)*2,frames,f);
        if(n==0) break;
        for(size_t i=0;i<n;i++){
            double l=inbuf[i*2]/32768.0, r=inbuf[i*2+1]/32768.0;
            double low_l=run_path(&L_low,l), low_r=run_path(&R_low,r);
            double mid_l=run_path(&L_mid,l), mid_r=run_path(&R_mid,r);
            double high_l=run_path(&L_high,l), high_r=run_path(&R_high,r);
            int16_t *o=outbuf+i*8;
            o[0]=clamp16(mid_l*32768.0);
            o[1]=clamp16(low_r*32768.0);
            o[2]=clamp16(low_l*32768.0);
            o[3]=clamp16(mid_r*32768.0);
            o[4]=0; o[5]=clamp16(high_r*32768.0); o[6]=clamp16(high_l*32768.0); o[7]=0;
        }
        unsigned count=pcm_frames_to_bytes(pcm,n);
        if(pcm_write(pcm,outbuf,count)!=0){fprintf(stderr,"write err: %s\n",pcm_get_error(pcm));return 1;}
        total+=n;
    }
    pcm_close(pcm);
    fprintf(stderr,"done frames=%zu\n",total);
    return 0;
}
