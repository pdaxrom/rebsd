/*
**
** LINPACK.C        Linpack benchmark, calculates FLOPS.
**                  (FLoating Point Operations Per Second)
**
** Translated to C by Bonnie Toy 5/88
**
** Modified by Will Menninger, 10/93, with these features:
**  (modified on 2/25/94  to fix a problem with daxpy  for
**   unequal increments or equal increments not equal to 1.
**     Jack Dongarra)
**
** - Defaults to double precision.
** - Averages ROLLed and UNROLLed performance.
** - User selectable array sizes.
** - Automatically does enough repetitions to take at least 10 CPU seconds.
** - Prints machine precision.
** - ANSI prototyping.
**
** To compile:  cc -O -o linpack linpack.c -lm
**
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <float.h>
#ifdef LINPACK_KERNEL_BENCH
#include <limits.h>
#endif

#define DP

#ifdef SP
#define ZERO        0.0
#define ONE         1.0
#define PREC        "Single"
#define BASE10DIG   FLT_DIG

typedef float   REAL;
#endif

#ifdef DP
#define ZERO        0.0e0
#define ONE         1.0e0
#define PREC        "Double"
#define BASE10DIG   DBL_DIG

typedef double  REAL;
#endif

static REAL linpack  (long nreps,int arsize);
static void matgen   (REAL *a,int lda,int n,REAL *b,REAL *norma);
static void dgefa    (REAL *a,int lda,int n,int *ipvt,int *info,int roll);
static void dgesl    (REAL *a,int lda,int n,int *ipvt,REAL *b,int job,int roll);
static void daxpy_r  (int n,REAL da,REAL *dx,int incx,REAL *dy,int incy);
static REAL ddot_r   (int n,REAL *dx,int incx,REAL *dy,int incy);
static void dscal_r  (int n,REAL da,REAL *dx,int incx);
static void daxpy_ur (int n,REAL da,REAL *dx,int incx,REAL *dy,int incy);
static REAL ddot_ur  (int n,REAL *dx,int incx,REAL *dy,int incy);
static void dscal_ur (int n,REAL da,REAL *dx,int incx);
static int  idamax   (int n,REAL *dx,int incx);
static REAL second   (void);
#ifdef LINPACK_KERNEL_BENCH
static int  kernel_bench_main(void);
#endif

static void *mempool;


#ifndef LINPACK_KERNEL_BENCH
int main(void)
{
    char    *arsize_input, *min_seconds_input;
    int     arsize;
    long    arsize2d,nreps;
    size_t  malloc_arg,memreq;
    REAL    min_seconds;

    arsize_input = getenv("LINPACK_ARRAY_SIZE");
    if (arsize_input == NULL) {
        arsize = 200;
    } else {
        arsize = atoi(arsize_input);
    }
    min_seconds_input = getenv("LINPACK_MIN_SECONDS");
    if (min_seconds_input == NULL) {
        min_seconds = 10.0;
    } else {
        min_seconds = (REAL)atof(min_seconds_input);
    }
    if (min_seconds <= ZERO) {
        min_seconds = ONE;
    }

        arsize/=2;
        arsize*=2;
        if (arsize<10)
            {
            printf("Too small.\n");
            return 1;
            }
        arsize2d = (long)arsize*(long)arsize;
        memreq=arsize2d*sizeof(REAL)+(long)arsize*sizeof(REAL)+(long)arsize*sizeof(int);
        printf("Memory required:  %ldK.\n",(memreq+512L)>>10);
        malloc_arg=(size_t)memreq;
        if (malloc_arg!=memreq || (mempool=malloc(malloc_arg))==NULL)
            {
            printf("Not enough memory available for given array size.\n\n");
            return 2;
            }
        printf("\n\nLINPACK benchmark, %s precision.\n",PREC);
        printf("Machine precision:  %d digits.\n",BASE10DIG);
        printf("Array size %d X %d.\n",arsize,arsize);
        printf("Average rolled and unrolled performance:\n\n");
        printf("    Reps Time(s) DGEFA   DGESL  OVERHEAD    KFLOPS\n");
        printf("----------------------------------------------------\n");
        nreps=1;
        while (linpack(nreps,arsize)<min_seconds)
            nreps*=2;
        free(mempool);
        printf("\n");
        return 0;
}
#else
int main(void)
{
    return kernel_bench_main();
}
#endif


static REAL linpack(long nreps,int arsize)

    {
    REAL  *a,*b;
    REAL   norma,t1,kflops,tdgesl,tdgefa,totalt,toverhead,ops;
    int   *ipvt,n,info,lda;
    long   i,arsize2d;

    lda = arsize;
    n = arsize/2;
    arsize2d = (long)arsize*(long)arsize;
    ops=((2.0*n*n*n)/3.0+2.0*n*n);
    a=(REAL *)mempool;
    b=a+arsize2d;
    ipvt=(int *)&b[arsize];
    tdgesl=0;
    tdgefa=0;
    totalt=second();
    for (i=0;i<nreps;i++)
        {
        matgen(a,lda,n,b,&norma);
        t1 = second();
        dgefa(a,lda,n,ipvt,&info,1);
        tdgefa += second()-t1;
        t1 = second();
        dgesl(a,lda,n,ipvt,b,0,1);
        tdgesl += second()-t1;
        }
    for (i=0;i<nreps;i++)
        {
        matgen(a,lda,n,b,&norma);
        t1 = second();
        dgefa(a,lda,n,ipvt,&info,0);
        tdgefa += second()-t1;
        t1 = second();
        dgesl(a,lda,n,ipvt,b,0,0);
        tdgesl += second()-t1;
        }
    totalt=second()-totalt;
    if (totalt<0.5 || tdgefa+tdgesl<0.2)
        return(0.);
    kflops=2.*nreps*ops/(1000.*(tdgefa+tdgesl));
    toverhead=totalt-tdgefa-tdgesl;
    if (tdgefa<0.)
        tdgefa=0.;
    if (tdgesl<0.)
        tdgesl=0.;
    if (toverhead<0.)
        toverhead=0.;
    printf("%8ld %6.2f %6.2f%% %6.2f%% %6.2f%%  %9.3f\n",
            nreps,totalt,100.*tdgefa/totalt,
            100.*tdgesl/totalt,100.*toverhead/totalt,
            kflops);
    return(totalt);
    }


/*
** For matgen,
** We would like to declare a[][lda], but c does not allow it.  In this
** function, references to a[i][j] are written a[lda*i+j].
*/
static void matgen(REAL *a,int lda,int n,REAL *b,REAL *norma)

    {
    int init,i,j;

    init = 1325;
    *norma = 0.0;
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++)
            {
            init = (int)((long)3125*(long)init % 65536L);
            a[lda*j+i] = (init - 32768.0)/16384.0;
            *norma = (a[lda*j+i] > *norma) ? a[lda*j+i] : *norma;
            }
    for (i = 0; i < n; i++)
        b[i] = 0.0;
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++)
            b[i] = b[i] + a[lda*j+i];
    }


/*
**
** DGEFA benchmark
**
** We would like to declare a[][lda], but c does not allow it.  In this
** function, references to a[i][j] are written a[lda*i+j].
**
**   dgefa factors a double precision matrix by gaussian elimination.
**
**   dgefa is usually called by dgeco, but it can be called
**   directly with a saving in time if  rcond  is not needed.
**   (time for dgeco) = (1 + 9/n)*(time for dgefa) .
**
**   on entry
**
**      a       REAL precision[n][lda]
**              the matrix to be factored.
**
**      lda     integer
**              the leading dimension of the array  a .
**
**      n       integer
**              the order of the matrix  a .
**
**   on return
**
**      a       an upper triangular matrix and the multipliers
**              which were used to obtain it.
**              the factorization can be written  a = l*u  where
**              l  is a product of permutation and unit lower
**              triangular matrices and  u  is upper triangular.
**
**      ipvt    integer[n]
**              an integer vector of pivot indices.
**
**      info    integer
**              = 0  normal value.
**              = k  if  u[k][k] .eq. 0.0 .  this is not an error
**                   condition for this subroutine, but it does
**                   indicate that dgesl or dgedi will divide by zero
**                   if called.  use  rcond  in dgeco for a reliable
**                   indication of singularity.
**
**   linpack. this version dated 08/14/78 .
**   cleve moler, university of New Mexico, argonne national lab.
**
**   functions
**
**   blas daxpy,dscal,idamax
**
*/
static void dgefa(REAL *a,int lda,int n,int *ipvt,int *info,int roll)

    {
    REAL t;
    int idamax(),j,k,kp1,l,nm1;

    /* gaussian elimination with partial pivoting */

    if (roll)
        {
        *info = 0;
        nm1 = n - 1;
        if (nm1 >=  0)
            for (k = 0; k < nm1; k++)
                {
                kp1 = k + 1;

                /* find l = pivot index */

                l = idamax(n-k,&a[lda*k+k],1) + k;
                ipvt[k] = l;

                /* zero pivot implies this column already
                   triangularized */

                if (a[lda*k+l] != ZERO)
                    {

                    /* interchange if necessary */

                    if (l != k)
                        {
                        t = a[lda*k+l];
                        a[lda*k+l] = a[lda*k+k];
                        a[lda*k+k] = t;
                        }

                    /* compute multipliers */

                    t = -ONE/a[lda*k+k];
                    dscal_r(n-(k+1),t,&a[lda*k+k+1],1);

                    /* row elimination with column indexing */

                    for (j = kp1; j < n; j++)
                        {
                        t = a[lda*j+l];
                        if (l != k)
                            {
                            a[lda*j+l] = a[lda*j+k];
                            a[lda*j+k] = t;
                            }
                        daxpy_r(n-(k+1),t,&a[lda*k+k+1],1,&a[lda*j+k+1],1);
                        }
                    }
                else
                    (*info) = k;
                }
        ipvt[n-1] = n-1;
        if (a[lda*(n-1)+(n-1)] == ZERO)
            (*info) = n-1;
        }
    else
        {
        *info = 0;
        nm1 = n - 1;
        if (nm1 >=  0)
            for (k = 0; k < nm1; k++)
                {
                kp1 = k + 1;

                /* find l = pivot index */

                l = idamax(n-k,&a[lda*k+k],1) + k;
                ipvt[k] = l;

                /* zero pivot implies this column already
                   triangularized */

                if (a[lda*k+l] != ZERO)
                    {

                    /* interchange if necessary */

                    if (l != k)
                        {
                        t = a[lda*k+l];
                        a[lda*k+l] = a[lda*k+k];
                        a[lda*k+k] = t;
                        }

                    /* compute multipliers */

                    t = -ONE/a[lda*k+k];
                    dscal_ur(n-(k+1),t,&a[lda*k+k+1],1);

                    /* row elimination with column indexing */

                    for (j = kp1; j < n; j++)
                        {
                        t = a[lda*j+l];
                        if (l != k)
                            {
                            a[lda*j+l] = a[lda*j+k];
                            a[lda*j+k] = t;
                            }
                        daxpy_ur(n-(k+1),t,&a[lda*k+k+1],1,&a[lda*j+k+1],1);
                        }
                    }
                else
                    (*info) = k;
                }
        ipvt[n-1] = n-1;
        if (a[lda*(n-1)+(n-1)] == ZERO)
            (*info) = n-1;
        }
    }


/*
**
** DGESL benchmark
**
** We would like to declare a[][lda], but c does not allow it.  In this
** function, references to a[i][j] are written a[lda*i+j].
**
**   dgesl solves the double precision system
**   a * x = b  or  trans(a) * x = b
**   using the factors computed by dgeco or dgefa.
**
**   on entry
**
**      a       double precision[n][lda]
**              the output from dgeco or dgefa.
**
**      lda     integer
**              the leading dimension of the array  a .
**
**      n       integer
**              the order of the matrix  a .
**
**      ipvt    integer[n]
**              the pivot vector from dgeco or dgefa.
**
**      b       double precision[n]
**              the right hand side vector.
**
**      job     integer
**              = 0         to solve  a*x = b ,
**              = nonzero   to solve  trans(a)*x = b  where
**                          trans(a)  is the transpose.
**
**  on return
**
**      b       the solution vector  x .
**
**   error condition
**
**      a division by zero will occur if the input factor contains a
**      zero on the diagonal.  technically this indicates singularity
**      but it is often caused by improper arguments or improper
**      setting of lda .  it will not occur if the subroutines are
**      called correctly and if dgeco has set rcond .gt. 0.0
**      or dgefa has set info .eq. 0 .
**
**   to compute  inverse(a) * c  where  c  is a matrix
**   with  p  columns
**         dgeco(a,lda,n,ipvt,rcond,z)
**         if (!rcond is too small){
**              for (j=0,j<p,j++)
**                      dgesl(a,lda,n,ipvt,c[j][0],0);
**         }
**
**   linpack. this version dated 08/14/78 .
**   cleve moler, university of new mexico, argonne national lab.
**
**   functions
**
**   blas daxpy,ddot
*/
static void dgesl(REAL *a,int lda,int n,int *ipvt,REAL *b,int job,int roll)

    {
    REAL    t;
    int     k,kb,l,nm1;

    if (roll)
        {
        nm1 = n - 1;
        if (job == 0)
            {

            /* job = 0 , solve  a * x = b   */
            /* first solve  l*y = b         */

            if (nm1 >= 1)
                for (k = 0; k < nm1; k++)
                    {
                    l = ipvt[k];
                    t = b[l];
                    if (l != k)
                        {
                        b[l] = b[k];
                        b[k] = t;
                        }
                    daxpy_r(n-(k+1),t,&a[lda*k+k+1],1,&b[k+1],1);
                    }

            /* now solve  u*x = y */

            for (kb = 0; kb < n; kb++)
                {
                k = n - (kb + 1);
                b[k] = b[k]/a[lda*k+k];
                t = -b[k];
                daxpy_r(k,t,&a[lda*k+0],1,&b[0],1);
                }
            }
        else
            {

            /* job = nonzero, solve  trans(a) * x = b  */
            /* first solve  trans(u)*y = b             */

            for (k = 0; k < n; k++)
                {
                t = ddot_r(k,&a[lda*k+0],1,&b[0],1);
                b[k] = (b[k] - t)/a[lda*k+k];
                }

            /* now solve trans(l)*x = y     */

            if (nm1 >= 1)
                for (kb = 1; kb < nm1; kb++)
                    {
                    k = n - (kb+1);
                    b[k] = b[k] + ddot_r(n-(k+1),&a[lda*k+k+1],1,&b[k+1],1);
                    l = ipvt[k];
                    if (l != k)
                        {
                        t = b[l];
                        b[l] = b[k];
                        b[k] = t;
                        }
                    }
            }
        }
    else
        {
        nm1 = n - 1;
        if (job == 0)
            {

            /* job = 0 , solve  a * x = b   */
            /* first solve  l*y = b         */

            if (nm1 >= 1)
                for (k = 0; k < nm1; k++)
                    {
                    l = ipvt[k];
                    t = b[l];
                    if (l != k)
                        {
                        b[l] = b[k];
                        b[k] = t;
                        }
                    daxpy_ur(n-(k+1),t,&a[lda*k+k+1],1,&b[k+1],1);
                    }

            /* now solve  u*x = y */

            for (kb = 0; kb < n; kb++)
                {
                k = n - (kb + 1);
                b[k] = b[k]/a[lda*k+k];
                t = -b[k];
                daxpy_ur(k,t,&a[lda*k+0],1,&b[0],1);
                }
            }
        else
            {

            /* job = nonzero, solve  trans(a) * x = b  */
            /* first solve  trans(u)*y = b             */

            for (k = 0; k < n; k++)
                {
                t = ddot_ur(k,&a[lda*k+0],1,&b[0],1);
                b[k] = (b[k] - t)/a[lda*k+k];
                }

            /* now solve trans(l)*x = y     */

            if (nm1 >= 1)
                for (kb = 1; kb < nm1; kb++)
                    {
                    k = n - (kb+1);
                    b[k] = b[k] + ddot_ur(n-(k+1),&a[lda*k+k+1],1,&b[k+1],1);
                    l = ipvt[k];
                    if (l != k)
                        {
                        t = b[l];
                        b[l] = b[k];
                        b[k] = t;
                        }
                    }
            }
        }
    }



/*
** Constant times a vector plus a vector.
** Jack Dongarra, linpack, 3/11/78.
** ROLLED version
*/
static void daxpy_r(int n,REAL da,REAL *dx,int incx,REAL *dy,int incy)

    {
    int i,ix,iy;

    if (n <= 0)
        return;
    if (da == ZERO)
        return;

    if (incx != 1 || incy != 1)
        {

        /* code for unequal increments or equal increments != 1 */

        ix = 1;
        iy = 1;
        if(incx < 0) ix = (-n+1)*incx + 1;
        if(incy < 0)iy = (-n+1)*incy + 1;
        for (i = 0;i < n; i++)
            {
            dy[iy] = dy[iy] + da*dx[ix];
            ix = ix + incx;
            iy = iy + incy;
            }
        return;
        }

    /* code for both increments equal to 1 */

    for (i = 0;i < n; i++)
        dy[i] = dy[i] + da*dx[i];
    }


/*
** Forms the dot product of two vectors.
** Jack Dongarra, linpack, 3/11/78.
** ROLLED version
*/
static REAL ddot_r(int n,REAL *dx,int incx,REAL *dy,int incy)

    {
    REAL dtemp;
    int i,ix,iy;

    dtemp = ZERO;

    if (n <= 0)
        return(ZERO);

    if (incx != 1 || incy != 1)
        {

        /* code for unequal increments or equal increments != 1 */

        ix = 0;
        iy = 0;
        if (incx < 0) ix = (-n+1)*incx;
        if (incy < 0) iy = (-n+1)*incy;
        for (i = 0;i < n; i++)
            {
            dtemp = dtemp + dx[ix]*dy[iy];
            ix = ix + incx;
            iy = iy + incy;
            }
        return(dtemp);
        }

    /* code for both increments equal to 1 */

    for (i=0;i < n; i++)
        dtemp = dtemp + dx[i]*dy[i];
    return(dtemp);
    }


/*
** Scales a vector by a constant.
** Jack Dongarra, linpack, 3/11/78.
** ROLLED version
*/
static void dscal_r(int n,REAL da,REAL *dx,int incx)

    {
    int i,nincx;

    if (n <= 0)
        return;
    if (incx != 1)
        {

        /* code for increment not equal to 1 */

        nincx = n*incx;
        for (i = 0; i < nincx; i = i + incx)
            dx[i] = da*dx[i];
        return;
        }

    /* code for increment equal to 1 */

    for (i = 0; i < n; i++)
        dx[i] = da*dx[i];
    }


/*
** constant times a vector plus a vector.
** Jack Dongarra, linpack, 3/11/78.
** UNROLLED version
*/
static void daxpy_ur(int n,REAL da,REAL *dx,int incx,REAL *dy,int incy)

    {
    int i,ix,iy,m;

    if (n <= 0)
        return;
    if (da == ZERO)
        return;

    if (incx != 1 || incy != 1)
        {

        /* code for unequal increments or equal increments != 1 */

        ix = 1;
        iy = 1;
        if(incx < 0) ix = (-n+1)*incx + 1;
        if(incy < 0)iy = (-n+1)*incy + 1;
        for (i = 0;i < n; i++)
            {
            dy[iy] = dy[iy] + da*dx[ix];
            ix = ix + incx;
            iy = iy + incy;
            }
        return;
        }

    /* code for both increments equal to 1 */

    m = n % 4;
    if ( m != 0)
        {
        for (i = 0; i < m; i++)
            dy[i] = dy[i] + da*dx[i];
        if (n < 4)
            return;
        }
    for (i = m; i < n; i = i + 4)
        {
        dy[i] = dy[i] + da*dx[i];
        dy[i+1] = dy[i+1] + da*dx[i+1];
        dy[i+2] = dy[i+2] + da*dx[i+2];
        dy[i+3] = dy[i+3] + da*dx[i+3];
        }
    }


/*
** Forms the dot product of two vectors.
** Jack Dongarra, linpack, 3/11/78.
** UNROLLED version
*/
static REAL ddot_ur(int n,REAL *dx,int incx,REAL *dy,int incy)

    {
    REAL dtemp;
    int i,ix,iy,m;

    dtemp = ZERO;

    if (n <= 0)
        return(ZERO);

    if (incx != 1 || incy != 1)
        {

        /* code for unequal increments or equal increments != 1 */

        ix = 0;
        iy = 0;
        if (incx < 0) ix = (-n+1)*incx;
        if (incy < 0) iy = (-n+1)*incy;
        for (i = 0;i < n; i++)
            {
            dtemp = dtemp + dx[ix]*dy[iy];
            ix = ix + incx;
            iy = iy + incy;
            }
        return(dtemp);
        }

    /* code for both increments equal to 1 */

    m = n % 5;
    if (m != 0)
        {
        for (i = 0; i < m; i++)
            dtemp = dtemp + dx[i]*dy[i];
        if (n < 5)
            return(dtemp);
        }
    for (i = m; i < n; i = i + 5)
        {
        dtemp = dtemp + dx[i]*dy[i] +
        dx[i+1]*dy[i+1] + dx[i+2]*dy[i+2] +
        dx[i+3]*dy[i+3] + dx[i+4]*dy[i+4];
        }
    return(dtemp);
    }


/*
** Scales a vector by a constant.
** Jack Dongarra, linpack, 3/11/78.
** UNROLLED version
*/
static void dscal_ur(int n,REAL da,REAL *dx,int incx)

    {
    int i,m,nincx;

    if (n <= 0)
        return;
    if (incx != 1)
        {

        /* code for increment not equal to 1 */

        nincx = n*incx;
        for (i = 0; i < nincx; i = i + incx)
            dx[i] = da*dx[i];
        return;
        }

    /* code for increment equal to 1 */

    m = n % 5;
    if (m != 0)
        {
        for (i = 0; i < m; i++)
            dx[i] = da*dx[i];
        if (n < 5)
            return;
        }
    for (i = m; i < n; i = i + 5)
        {
        dx[i] = da*dx[i];
        dx[i+1] = da*dx[i+1];
        dx[i+2] = da*dx[i+2];
        dx[i+3] = da*dx[i+3];
        dx[i+4] = da*dx[i+4];
        }
    }


/*
** Finds the index of element having max. absolute value.
** Jack Dongarra, linpack, 3/11/78.
*/
static int idamax(int n,REAL *dx,int incx)

    {
    REAL dmax;
    int i, ix, itemp;

    if (n < 1)
        return(-1);
    if (n ==1 )
        return(0);
    if(incx != 1)
        {

        /* code for increment not equal to 1 */

        ix = 1;
        dmax = fabs((double)dx[0]);
        ix = ix + incx;
        for (i = 1; i < n; i++)
            {
            if(fabs((double)dx[ix]) > dmax)
                {
                itemp = i;
                dmax = fabs((double)dx[ix]);
                }
            ix = ix + incx;
            }
        }
    else
        {

        /* code for increment equal to 1 */

        itemp = 0;
        dmax = fabs((double)dx[0]);
        for (i = 1; i < n; i++)
            if(fabs((double)dx[i]) > dmax)
                {
                itemp = i;
                dmax = fabs((double)dx[i]);
                }
        }
    return (itemp);
    }


static REAL second(void)

    {
    struct timeval tv;

    if (gettimeofday(&tv, (struct timezone *)0) < 0)
        return ZERO;
    return ((REAL)tv.tv_sec + (REAL)tv.tv_usec / 1000000.0);
    }

#ifdef LINPACK_KERNEL_BENCH

typedef REAL (*kernel_bench_fn)(long);
typedef void (*kernel_daxpy_fn)(int, REAL, REAL *, int, REAL *, int);
typedef REAL (*kernel_ddot_fn)(int, REAL *, int, REAL *, int);
typedef void (*kernel_dscal_fn)(int, REAL, REAL *, int);
typedef int (*kernel_idamax_fn)(int, REAL *, int);

static kernel_daxpy_fn volatile kernel_daxpy_r_fn = daxpy_r;
static kernel_daxpy_fn volatile kernel_daxpy_ur_fn = daxpy_ur;
static kernel_ddot_fn volatile kernel_ddot_r_fn = ddot_r;
static kernel_ddot_fn volatile kernel_ddot_ur_fn = ddot_ur;
static kernel_dscal_fn volatile kernel_dscal_r_fn = dscal_r;
static kernel_dscal_fn volatile kernel_dscal_ur_fn = dscal_ur;
static kernel_idamax_fn volatile kernel_idamax_ptr = idamax;

static REAL *kernel_x;
static REAL *kernel_y;
static void *kernel_mempool;
static int kernel_n;
static REAL kernel_min_seconds;
static volatile REAL kernel_sink;

static int
kernel_close(REAL actual, REAL expected)
{
    REAL limit;

    limit = 1.0e-10 * (fabs((double)expected) + ONE);
    return fabs((double)(actual - expected)) <= limit;
}

static void
kernel_init_vectors(void)
{
    int i;

    for (i = 0; i <= kernel_n; i++) {
        kernel_x[i] = (REAL)((i % 17) - 8) / 17.0;
        kernel_y[i] = (REAL)((i % 13) - 6) / 13.0;
    }
}

static int
kernel_selftest(void)
{
    REAL actual, expected, x[8], y[8];
    int i, index;

    for (i = 0; i < 8; i++) {
        x[i] = (REAL)(i + 1);
        y[i] = (REAL)(8 - i);
    }
    expected = ZERO;
    for (i = 0; i < 8; i++)
        expected += x[i] * y[i];
    actual = ddot_r(8, x, 1, y, 1);
    if (!kernel_close(actual, expected))
        return 1;
    actual = ddot_ur(8, x, 1, y, 1);
    if (!kernel_close(actual, expected))
        return 2;

    daxpy_r(8, 0.5, x, 1, y, 1);
    for (i = 0; i < 8; i++)
        if (!kernel_close(y[i], (REAL)(8 - i) + 0.5 * x[i]))
            return 3;
    for (i = 0; i < 8; i++)
        y[i] = (REAL)(8 - i);
    daxpy_ur(8, 0.5, x, 1, y, 1);
    for (i = 0; i < 8; i++)
        if (!kernel_close(y[i], (REAL)(8 - i) + 0.5 * x[i]))
            return 4;

    dscal_r(8, 0.5, x, 1);
    for (i = 0; i < 8; i++)
        if (!kernel_close(x[i], 0.5 * (REAL)(i + 1)))
            return 5;
    for (i = 0; i < 8; i++)
        x[i] = (REAL)(i + 1);
    dscal_ur(8, 0.5, x, 1);
    for (i = 0; i < 8; i++)
        if (!kernel_close(x[i], 0.5 * (REAL)(i + 1)))
            return 6;

    for (i = 0; i < 8; i++)
        x[i] = (REAL)i;
    x[5] = -20.0;
    index = idamax(8, x, 1);
    if (index != 5) {
        printf("LINPACK_KERNEL_SELFTEST_IDAMAX actual=%d x5=%.17g x7=%.17g\n",
            index, x[5], x[7]);
        return 7;
    }
    return 0;
}

static REAL
kernel_daxpy_r(long reps)
{
    long i;

    for (i = 0; i < reps; i++)
        (*kernel_daxpy_r_fn)(kernel_n, 1.0e-6, kernel_x, 1, kernel_y, 1);
    return kernel_y[0] + kernel_y[kernel_n - 1];
}

static REAL
kernel_daxpy_ur(long reps)
{
    long i;

    for (i = 0; i < reps; i++)
        (*kernel_daxpy_ur_fn)(kernel_n, 1.0e-6, kernel_x, 1, kernel_y, 1);
    return kernel_y[0] + kernel_y[kernel_n - 1];
}

static REAL
kernel_ddot_r(long reps)
{
    REAL result;
    long i;

    result = ZERO;
    for (i = 0; i < reps; i++)
        result += (*kernel_ddot_r_fn)(kernel_n, &kernel_x[i & 1], 1,
            &kernel_y[i & 1], 1);
    return result;
}

static REAL
kernel_ddot_ur(long reps)
{
    REAL result;
    long i;

    result = ZERO;
    for (i = 0; i < reps; i++)
        result += (*kernel_ddot_ur_fn)(kernel_n, &kernel_x[i & 1], 1,
            &kernel_y[i & 1], 1);
    return result;
}

static REAL
kernel_dscal_r(long reps)
{
    long i;

    for (i = 0; i < reps; i++)
        (*kernel_dscal_r_fn)(kernel_n, 0.999999, kernel_x, 1);
    return kernel_x[0] + kernel_x[kernel_n - 1];
}

static REAL
kernel_dscal_ur(long reps)
{
    long i;

    for (i = 0; i < reps; i++)
        (*kernel_dscal_ur_fn)(kernel_n, 0.999999, kernel_x, 1);
    return kernel_x[0] + kernel_x[kernel_n - 1];
}

static REAL
kernel_idamax(long reps)
{
    long i;
    int result;

    result = 0;
    for (i = 0; i < reps; i++)
        result += (*kernel_idamax_ptr)(kernel_n, &kernel_x[i & 1], 1);
    return (REAL)result;
}

static int
kernel_run(const char *name, kernel_bench_fn fn)
{
    REAL elapsed, melem, result, start;
    long reps;

    reps = 1;
    for (;;) {
        kernel_init_vectors();
        start = second();
        result = fn(reps);
        elapsed = second() - start;
        if (elapsed >= kernel_min_seconds)
            break;
        if (reps > LONG_MAX / 2) {
            printf("LINPACK_KERNEL_ERROR %s repetition-overflow\n", name);
            return 1;
        }
        reps *= 2;
    }
    kernel_sink += result;
    melem = (REAL)reps * (REAL)kernel_n / (elapsed * 1000000.0);
    printf("LINPACK_KERNEL_RESULT %s reps=%ld seconds=%.6f "
        "melem_s=%.3f checksum=%.9e\n",
        name, reps, elapsed, melem, result);
    return 0;
}

static int
kernel_bench_main(void)
{
    char *input;
    unsigned long aligned;
    size_t bytes;
    int fails, selftest;

    input = getenv("LINPACK_KERNEL_ARRAY_SIZE");
    kernel_n = input == NULL ? 120 : atoi(input);
    input = getenv("LINPACK_KERNEL_MIN_SECONDS");
    kernel_min_seconds = input == NULL ? ONE : (REAL)atof(input);
    if (kernel_n < 10 || kernel_n > 4096 || kernel_min_seconds <= ZERO) {
        printf("LINPACK_KERNEL_CONFIG_ERROR n=%d min_seconds=%.6f\n",
            kernel_n, kernel_min_seconds);
        return 1;
    }
    bytes = (size_t)(kernel_n + 1) * sizeof(*kernel_x);
    kernel_mempool = malloc(bytes * 2 + sizeof(*kernel_x) - 1);
    if (kernel_mempool == NULL) {
        printf("LINPACK_KERNEL_ALLOC_ERROR n=%d\n", kernel_n);
        return 2;
    }
    aligned = ((unsigned long)kernel_mempool + sizeof(*kernel_x) - 1) &
        ~((unsigned long)sizeof(*kernel_x) - 1);
    kernel_x = (REAL *)aligned;
    kernel_y = kernel_x + kernel_n + 1;

    printf("LINPACK_KERNEL_BEGIN n=%d min_seconds=%.3f\n",
        kernel_n, kernel_min_seconds);
    selftest = kernel_selftest();
    printf("LINPACK_KERNEL_SELFTEST %d\n", selftest);
    fails = selftest != 0;
    if (!fails) {
        fails += kernel_run("daxpy_r", kernel_daxpy_r);
        fails += kernel_run("daxpy_ur", kernel_daxpy_ur);
        fails += kernel_run("ddot_r", kernel_ddot_r);
        fails += kernel_run("ddot_ur", kernel_ddot_ur);
        fails += kernel_run("dscal_r", kernel_dscal_r);
        fails += kernel_run("dscal_ur", kernel_dscal_ur);
        fails += kernel_run("idamax", kernel_idamax);
    }
    printf("LINPACK_KERNEL_SINK %.9e\n", kernel_sink);
    printf("LINPACK_KERNEL_END %d\n", fails);
    free(kernel_mempool);
    return fails ? 1 : 0;
}

#endif
