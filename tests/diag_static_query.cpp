// DIAGNOSTICO (no es un test): costo por consulta (ns) sobre una raiz estatica pura, sin actualizaciones.
// Sirve para vigilar el costo de la indireccion vista->buffer a medida que avanzan los pasos del plan.
// Uso: diag_static_query <log2_n> [num_consultas=4000000]   (compilar con -O3 -DNDEBUG -march=native)
#include <adaptive/adaptive.hpp>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace adaptive;
int main(int argc,char**argv){
  int log_n=atoi(argv[1]); uint64_t Q=argc>2?atoll(argv[2]):4000000;
  uint64_t n=1ULL<<log_n, s=88172645463325252ULL; auto rnd=[&]{s^=s<<13;s^=s>>7;s^=s<<17;return s;};
  std::vector<uint64_t> w(n/64); for(auto&x:w)x=rnd();
  bit_vector<> bv(w.data(), n);
  std::vector<uint64_t> pos(Q), sel(Q); for(uint64_t k=0;k<Q;++k){ pos[k]=rnd()%n; sel[k]=1+rnd()%bv.ones(); }
  auto timeit=[&](auto f){ double best=1e18; uint64_t chk=0; for(int rep=0;rep<5;++rep){ auto t0=std::chrono::steady_clock::now(); uint64_t c=0; for(uint64_t k=0;k<Q;++k) c+=f(k); double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count()/Q; if(ns<best)best=ns; chk=c;} return std::pair<double,uint64_t>(best,chk); };
  auto a=timeit([&](uint64_t k){return (uint64_t)bv.access(pos[k]);});
  auto r=timeit([&](uint64_t k){return bv.rank1(pos[k]);});
  auto sl=timeit([&](uint64_t k){return bv.select1(sel[k]);});
  std::printf("access %.1f ns | rank %.1f ns | select %.1f ns   (chk %lu %lu %lu)\n",a.first,r.first,sl.first,a.second,r.second,sl.second);
}
