#ifndef PARALLEL_RNG_H
#define PARALLEL_RNG_H

#ifdef __cplusplus
extern "C" {
#endif
typedef struct threadsafe_rng{
    void* local_rng;
    void* local_uniform;
} threadsafe_rng;

threadsafe_rng create_threadsafe_rng();

void delete_threadsafe_rng(threadsafe_rng del_tsrng);

double threadsafe_rng_next(threadsafe_rng tsrng);

void do_nothing();


#ifdef __cplusplus
}
#endif


#endif