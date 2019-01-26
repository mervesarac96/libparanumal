/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "elliptic.h"

void ellipticOasSolve(elliptic_t *elliptic, dfloat lambda,
		      occa::memory &o_r, occa::memory &o_z){

  // TW: do not interleave these tasks yet
  
  // 1. restrict to coarse grid
  precon_t *precon = elliptic->precon;
  mesh_t *mesh = elliptic->mesh;

  elliptic_t *elliptic1 = (elliptic_t*) precon->ellipticOneRing; // should rename
  mesh_t *mesh1 = elliptic1->mesh;
  
  //  precon->oasRestrictionKernel(mesh->Nelements, precon->o_oasRestrictionMatrix, o_r, precon->o_oasCoarseTmp);

  // 2. solve coarse problem
  //   a. call solver
  //   b. prolongate (watch out for +=)

  // 3. collect patch rhs  
  //  if (elliptic->Nmasked) mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_r);

  // hack to zero initial guess
  dfloat *h_x = (dfloat*) calloc(mesh1->Np*mesh1->Nelements, sizeof(dfloat));
  elliptic1->o_x.copyFrom(h_x);
  elliptic1->o_r.copyFrom(h_x);

  mesh1->device.finish();
  mesh->device.finish();
  
  ellipticOneRingExchange(elliptic, elliptic1, mesh1->Np*sizeof(dfloat), o_r, elliptic1->o_r);

  mesh1->device.finish();
  mesh->device.finish();
  
  dfloat tol = 1.e-2;

  // patch solve
  if(mesh->rank==0) printf("Starting extended partition iterations:\n");

  // TW turned off
  ellipticSolve(elliptic1, lambda, tol, elliptic1->o_r, elliptic1->o_x); // may need to zero o_x

  // sum up patches
  ogsGatherScatter(elliptic1->o_x, ogsDfloat, ogsAdd, elliptic->precon->oasOgs);

  // do we need to scale by 1/overlapDegree ?
  
  // just retain core [ actually need to gs all the element contributions]
  //  o_z.copyFrom(elliptic1->o_x, mesh->Nelements*mesh->Np*sizeof(dfloat), 0);
  
  elliptic->dotMultiplyKernel(mesh->Nelements*mesh->Np, elliptic->precon->oasOgs->o_invDegree, elliptic1->o_x, o_z);

  // TW: 
  //  if (elliptic->Nmasked) mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_z);
  
  free(h_x);
}
