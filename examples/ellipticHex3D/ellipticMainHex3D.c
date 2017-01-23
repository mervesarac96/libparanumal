#include "ellipticHex3D.h"

void ellipticParallelGatherScatter3D(mesh3D *mesh, occa::memory &o_q, occa::memory &o_gsq, const char *type){

  mesh->device.finish();
  occa::tic("meshParallelGatherScatter3D");
  
  // use gather map for gather and scatter
  meshParallelGatherScatter3D(mesh,
			      mesh->NuniqueBases,  // gather info for DEVICE gather
			      mesh->o_gatherNodeOffsets,
			      mesh->o_gatherLocalNodes,
			      mesh->o_gatherTmp,     
			      mesh->NnodeHalo,     // halo info for extracting gathered halo
			      mesh->o_nodeHaloIds,
			      mesh->o_subGatherTmp,
			      mesh->subGatherTmp,
			      mesh->NuniqueBases,   // use gather info for DEVICE scatter
			      mesh->o_gatherNodeOffsets,
			      mesh->o_gatherLocalNodes,
			      mesh->gsh,            // gslib 
			      o_q,
			      o_gsq,
			      type);

  mesh->device.finish();
  occa::toc("meshParallelGatherScatter3D");

  
}

void ellipticOperator(mesh3D *mesh, dfloat lambda, occa::memory &o_q, occa::memory &o_Aq){

  mesh->device.finish();
  occa::tic("AxKernel");
  
  // compute local element operations and store result in o_Aq
  mesh->AxKernel(mesh->Nelements, mesh->o_ggeo, mesh->o_D, lambda, o_q, o_Aq); 

  mesh->device.finish();
  occa::toc("AxKernel");
  
  // do parallel gather scatter (uses o_gatherTmp,  o_subGatherTmp, subGatherTmp)
  ellipticParallelGatherScatter3D(mesh, o_Aq, o_Aq, dfloatString);
  
}

dfloat ellipticScaledAdd(mesh3D *mesh, dfloat alpha, occa::memory &o_a, dfloat beta, occa::memory &o_b){

  iint Ntotal = mesh->Nelements*mesh->Np;

  mesh->device.finish();
  occa::tic("scaledAddKernel");
  
  // b[n] = alpha*a[n] + beta*b[n] n\in [0,Ntotal)
  mesh->scaledAddKernel(Ntotal, alpha, o_a, beta, o_b);

  mesh->device.finish();
  occa::toc("scaledAddKernel");
  
}

dfloat ellipticWeightedInnerProduct(mesh3D *mesh,
				    iint Nblock,
				    occa::memory &o_w,
				    occa::memory &o_a,
				    occa::memory &o_b,
				    occa::memory &o_tmp,
				    dfloat *tmp){

  mesh->device.finish();
  occa::tic("weighted inner product2");

  iint Ntotal = mesh->Nelements*mesh->Np;
  mesh->weightedInnerProduct2Kernel(Ntotal, o_w, o_a, o_b, o_tmp);

  mesh->device.finish();
  occa::toc("weighted inner product2");

  
  o_tmp.copyTo(tmp);

  dfloat wab = 0;
  for(iint n=0;n<Nblock;++n){
    wab += tmp[n];
  }
      
  dfloat globalwab = 0;
  MPI_Allreduce(&wab, &globalwab, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);

  return globalwab;
}

dfloat ellipticWeightedInnerProduct(mesh3D *mesh,
				    iint Nblock,
				    occa::memory &o_w,
				    occa::memory &o_a,
				    occa::memory &o_tmp,
				    dfloat *tmp){

  iint Ntotal = mesh->Nelements*mesh->Np;

  mesh->device.finish();
  occa::tic("weighted inner product1");
  
  mesh->weightedInnerProduct1Kernel(Ntotal, o_w, o_a, o_tmp);

  mesh->device.finish();
  occa::toc("weighted inner product1");
  
  o_tmp.copyTo(tmp);
  
  dfloat wa2 = 0;
  for(iint n=0;n<Nblock;++n){
    wa2 += tmp[n];
  }
  
  dfloat globalwa2 = 0;
  MPI_Allreduce(&wa2, &globalwa2, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);

  return globalwa2;
}

void ellipticProject(mesh3D *mesh, occa::memory &o_v, occa::memory &o_Pv){


  iint Ntotal = mesh->Nelements*mesh->Np;
  
  mesh->dotMultiplyKernel(Ntotal, mesh->o_projectL2, o_v, o_Pv);

  // assemble
  ellipticParallelGatherScatter3D(mesh, o_Pv, o_Pv, dfloatString);
}
 

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  if(argc!=3){
    // to run cavity test case with degree N elements
    printf("usage: ./main meshes/cavityH005.msh N\n");
    exit(-1);
  }

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  
  // int specify polynomial degree 
  int N = atoi(argv[2]);
  
  // set up mesh stuff
  mesh3D *meshSetupHex3D(char *, iint);
  mesh3D *mesh = meshSetupHex3D(argv[1], N);

  // set up elliptic stuff
  int B = 256; // block size for reduction (hard coded)

  ellipticSetupHex3D(mesh);

  iint Ntotal = mesh->Np*mesh->Nelements;
  iint Nblock = (Ntotal+B-1)/B;
  
  // build degree vector
  for(iint n=0;n<Ntotal;++n)
    mesh->rhsq[n] = 1;

  mesh->o_rhsq.copyFrom(mesh->rhsq);
  
  ellipticParallelGatherScatter3D(mesh, mesh->o_rhsq, mesh->o_rhsq, dfloatString);
  
  mesh->o_rhsq.copyTo(mesh->rhsq);

  dfloat *invDegree = (dfloat*) calloc(Ntotal, sizeof(dfloat));
  for(iint n=0;n<Ntotal;++n){
    invDegree[n] = 1./(mesh->rhsq[n]);
    if(!mesh->rhsq[n]) printf("found zero degree at  node %d \n", n);
  }
  
  occa::memory o_invDegree = mesh->device.malloc(Ntotal*sizeof(dfloat), invDegree);

  dfloat *p   = (dfloat*) calloc(Ntotal, sizeof(dfloat));
  dfloat *r   = (dfloat*) calloc(Ntotal, sizeof(dfloat));
  dfloat *x   = (dfloat*) calloc(Ntotal, sizeof(dfloat));
  dfloat *Ap  = (dfloat*) calloc(Ntotal, sizeof(dfloat));
  dfloat *tmp = (dfloat*) calloc(Nblock, sizeof(dfloat));
  
  // at this point gather-scatter is available
  dfloat lambda = 10;

  // set up cg

  // convergence tolerance (currently absolute)
  const dfloat tol = 1e-5;

  // load rhs into r
  for(iint e=0;e<mesh->Nelements;++e){
    for(iint n=0;n<mesh->Np;++n){

      iint ggid = e*mesh->Np*mesh->Nggeo + n;
      dfloat wJ = mesh->ggeo[ggid+mesh->Np*GWJID];

      iint   id = e*mesh->Np+n;
      dfloat xn = mesh->x[id];
      dfloat yn = mesh->y[id];
      dfloat zn = mesh->z[id];

      dfloat f = -(3*M_PI*M_PI+lambda)*cos(M_PI*xn)*cos(M_PI*yn)*cos(M_PI*zn);
      //      dfloat f = -lambda*cos(M_PI*xn)*cos(M_PI*yn)*cos(M_PI*zn);
      //dfloat f = -lambda*xn*xn*xn;

      //      Zt*Z*(SL+lambda*ML)*Zt*x = Zt*Z*ML*fL
      
      r[id] = -wJ*f;

      x[id] = 0; // initial guess
    }
  }

  occa::initTimer(mesh->device);
  
  // need to rename o_r, o_x to avoid confusion
  occa::memory o_p   = mesh->device.malloc(Ntotal*sizeof(dfloat), p);
  occa::memory o_r   = mesh->device.malloc(Ntotal*sizeof(dfloat), r);
  occa::memory o_x   = mesh->device.malloc(Ntotal*sizeof(dfloat), x);
  occa::memory o_Ax  = mesh->device.malloc(Ntotal*sizeof(dfloat), x);
  occa::memory o_Ap  = mesh->device.malloc(Ntotal*sizeof(dfloat), Ap);
  occa::memory o_tmp = mesh->device.malloc(Nblock*sizeof(dfloat), tmp);
  
  // copy initial guess for x to DEVICE
  o_x.copyFrom(x);
  ellipticOperator(mesh, lambda, o_x, o_Ax); // eventually add reduction in scatterKernel
  
  // copy r = b
  o_r.copyFrom(r);

  // subtract r = b - A*x
  ellipticScaledAdd(mesh, -1.f, o_Ax, 1.f, o_r);

  // gather-scatter r
  ellipticParallelGatherScatter3D(mesh, o_r, o_r, dfloatString);

  // p = r
  o_p.copyFrom(o_r);
  
  // dot(r,r)
  dfloat rdotr0 = ellipticWeightedInnerProduct(mesh, Nblock, o_invDegree, o_r, o_tmp, tmp);
  dfloat rdotr1 = 0;

  do{
    // placeholder conjugate gradient:
    // https://en.wikipedia.org/wiki/Conjugate_gradient_method

    // -----------> merge these later into ellipticPcgPart1
    // A*p 
    ellipticOperator(mesh, lambda, o_p, o_Ap); // eventually add reduction in scatterKernel

    // dot(p,A*p)
    dfloat pAp = ellipticWeightedInnerProduct(mesh, Nblock, o_invDegree, o_p, o_Ap, o_tmp, tmp);
    // <------------ to here
    
    // alpha = dot(r,r)/dot(p,A*p)
    dfloat alpha = rdotr0/pAp;

    // --------------> merge these later into ellipticPcgPart2
    // x <= x + alpha*p
    ellipticScaledAdd(mesh,  alpha, o_p,  1.f, o_x);

    // r <= r - alpha*A*p
    ellipticScaledAdd(mesh, -alpha, o_Ap, 1.f, o_r);

    // dot(r,r)
    rdotr1 = ellipticWeightedInnerProduct(mesh, Nblock, o_invDegree, o_r, o_tmp, tmp);
    // <-------------- to here 
    
    if(rdotr1 < tol*tol) break;
    
    // beta = rdotr1/rdotr0
    dfloat beta = rdotr1/rdotr0;

    // p = r + beta*p
    ellipticScaledAdd(mesh, 1.f, o_r, beta, o_p);

    // swith rdotr0 <= rdotr1
    rdotr0 = rdotr1;

    if(rank==0)
      printf("pAp = %g norm(r) = %g\n", pAp, sqrt(rdotr0));

    //    ellipticProject(mesh, o_x, o_x);
    
  }while(rdotr0>(tol*tol));

  occa::printTimer();
  
  // arggh
  o_x.copyTo(mesh->q);

  dfloat maxError = 0;
  for(iint e=0;e<mesh->Nelements;++e){
    for(iint n=0;n<mesh->Np;++n){
      iint   id = e*mesh->Np+n;
      dfloat xn = mesh->x[id];
      dfloat yn = mesh->y[id];
      dfloat zn = mesh->z[id];
      dfloat exact = cos(M_PI*xn)*cos(M_PI*yn)*cos(M_PI*zn);
      //      dfloat exact = xn*xn*xn;
      dfloat error = fabs(exact-mesh->q[id]);
      
      maxError = mymax(maxError, error);
      //      if(error>1e-3)
      //	printf("exact: %g computed: %g error: %g\n", exact, mesh->q[id], error);

      mesh->q[id] -= exact;
    }
  }

  dfloat globalMaxError = 0;
  MPI_Allreduce(&maxError, &globalMaxError, 1, MPI_DFLOAT, MPI_MAX, MPI_COMM_WORLD);
  if(rank==0)
    printf("globalMaxError = %g\n", globalMaxError);
  
  meshPlotVTU3D(mesh, "foo", 0);
  
  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}
