#include	<string.h>
#include	"sgemm.c"
#include	<math.h>

static	inline	float	wyact(float	x){	return	(x/(1+(((int)(x>0)<<1)-1)*x));	}

static	inline	float	wygra(float	x){	return	((1-(((int)(x>0)<<1)-1)*x)*(1-(((int)(x>0)<<1)-1)*x));	}

template<unsigned	input,	unsigned	hidden,	unsigned	depth,	unsigned	output,	unsigned	batch>
static	inline	unsigned	wywsize(void){	return	(input+1)*hidden+(depth-1)*hidden*hidden+output*hidden;	}

template<unsigned	input,	unsigned	hidden,	unsigned	depth,	unsigned	output,	unsigned	batch>
static	inline	unsigned	wyasize(void){	return	2*depth*batch*hidden+batch*output;	}

static	inline	void	cblas_sgemm(char TrA, char TrB, int M, int N, int K, float Alpha, float *A, int lda, float *B, int ldb, float Beta, float *C, int ldc){
	SGEMM_(&TrA,&TrB,&M,&N,&K,&Alpha,A,&lda,B,&ldb,&Beta,C,&ldc);
}

template<unsigned	input,	unsigned	hidden,	unsigned	depth,	unsigned	output,	unsigned	batch>
float	wymlp(float	*weight,	float	**x,	float	**y,	float	eta,	float	*a) {
	#define	woff(i,l)	(weight+((l)?(input+1)*hidden+((l)-1)*hidden*hidden+(i)*hidden:(i)*hidden))
	#define	aoff(b,l)	(a+(l)*batch*hidden+(b)*hidden)
	#define	doff(b,l)	(d+(l)*batch*hidden+(b)*hidden)
	const	float	wh=1/sqrtf(hidden), wi=1/sqrtf(input+1);
	memset(a,	0,	batch*hidden*sizeof(float));
	float	*d=a+depth*batch*hidden,	*o=d+depth*batch*hidden,	*p,	*q,	*w;
	for(unsigned	b=0;	b<batch;	b++){
		p=aoff(b,0);
		for(unsigned	i=0;	i<input;	i++){
			w=woff(i,0);	float	s=x[b][i];
			for(unsigned	j=0;	j<hidden;	j++)	p[j]+=s*w[j];
		}
		w=woff(input,0);	p[0]=1;
		for(unsigned	j=1;	j<hidden;	j++)	p[j]=wyact(wi*(p[j]+w[j]));
	}
	float	ret=0;
	for(unsigned	l=1;	l<=depth;	l++){
	//	int	m=l==depth?output:hidden,	n=batch,	k=hidden,	lda=hidden,ldc=l==depth?output:hidden;	float	alpha=wh,beta=0;
	//	SGEMM_("Y","N",&m,&n,&k,&alpha,woff(0,l),&lda,aoff(0,l-1),&lda,&beta,l==depth?o:aoff(0,l),&ldc);
		cblas_sgemm('T','N',l==depth?output:hidden,batch,hidden,wh,woff(0,l),hidden,aoff(0,l-1),hidden,0,l==depth?o:aoff(0,l),l==depth?output:hidden);
		for(unsigned    b=0;    b<batch;    b++){
			if(l<depth){
				p=aoff(b,l);	p[0]=1;
				for(unsigned	j=1;	j<hidden;	j++)	p[j]=wyact(p[j]);
			}
			else{
				p=o+b*output;	
				for(unsigned	i=0;	i<output;	i++){
					p[i]-=y[b][i];
					ret+=p[i]*p[i];
					p[i]*=eta*wh;
				}
			}
		}
	}
	for(unsigned	l=depth;	l;	l--) {
		if(l<depth){
			for(unsigned	b=0;	b<batch;	b++){
				p=aoff(b,l);	q=doff(b,l);
				for(unsigned	i=0;	i<hidden;	i++)	q[i]*=wygra(p[i])*wh;
			}
		}
		cblas_sgemm('N','N',hidden,batch,l==depth?output:hidden,1,woff(0,l),hidden,l==depth?o:doff(0,l),l==depth?output:hidden,0,doff(0,l-1),hidden);
		cblas_sgemm('N','T',hidden,l==depth?output:hidden, batch,-1,aoff(0,l-1),hidden,l==depth?o:doff(0,l),l==depth?output:hidden,1,woff(0,l),hidden);
//		int	m=hidden,	n=batch,	k=l==depth?output:hidden,	lda=hidden,ldb=l==depth?output:hidden, ldc=hidden;	float	alpha=1,beta=0;
//		SGEMM_("N","N",&m,&n,&k,&alpha,woff(0,l),&lda,l==depth?o:doff(0,l),&ldb,&beta,doff(0,l-1),&ldc);
//		m=hidden,	n=l==depth?output:hidden,	k=batch,	lda=hidden,ldb=l==depth?output:hidden, ldc=hidden;	alpha=-1,beta=1;
//		SGEMM_("N","Y",&m,&n,&k,&alpha,aoff(0,l-1),&lda,l==depth?o:doff(0,l),&ldb,&beta,woff(0,l),&ldc);
	}
	for(unsigned    b=0;    b<batch;    b++){
		w=woff(input,0);	p=aoff(b,0);	q=doff(b,0);
		for(unsigned	j=0;	j<hidden;	j++){	q[j]*=wygra(p[j])*wi;	w[j]-=q[j];	}
		for(unsigned	i=0;	i<input;	i++){
			w=woff(i,0);	float	s=x[b][i];
			for(unsigned	j=0;	j<hidden;	j++)	w[j]-=s*q[j];
		}
	}
	return	ret;
}
/*
const	unsigned	input=16;
const	unsigned	hidden=64;
const	unsigned	output=8;
const	unsigned	depth=4;
const	unsigned	batch=128;
const	unsigned	fullbatch=1<<20;
#include	<iostream>
#include	<sys/time.h>
using	namespace	std;

int	main(void){
	float	*x[batch],	*y[batch],	z[input]={};
	for(unsigned	b=0;	b<batch;	b++)	x[b]=y[b]=z;
	size_t	wsize=wywsize<input,hidden,depth,output,batch>();
	size_t	asize=wyasize<input,hidden,depth,output,batch>();
	cerr<<wsize<<'\t'<<asize<<'\n';

	float	*w=(float*)aligned_alloc(64,wsize*sizeof(float));
	float	*a=(float*)aligned_alloc(64,asize*sizeof(float));

	timeval	beg,	end;	float	s=0;
	gettimeofday(&beg,NULL);
	for(unsigned	i=0;	i<fullbatch;	i+=batch)	s+=wymlp<input,hidden,depth,output,batch>(w,x,y,0.01,a);
	gettimeofday(&end,NULL);
	double	deltat=end.tv_sec-beg.tv_sec+1e-6*(end.tv_usec-beg.tv_usec);
	double	flops=(double)wsize*fullbatch*6;
	cerr<<1e-9*flops/deltat<<'\n';
	free(w);	free(a);
	return	s;
}
*/
