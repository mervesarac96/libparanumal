#include "elliptic.h"

// create elliptic and mesh structs for multigrid levels
elliptic_t *ellipticBuildMultigridLevel(elliptic_t *baseElliptic, int Nc, int Nf){

  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  elliptic_t *elliptic = (elliptic_t*) calloc(1, sizeof(elliptic_t));
  memcpy(elliptic,baseElliptic,sizeof(elliptic_t));

  //populate the mini-mesh using the mesh struct
  mesh_t *mesh = (mesh_t*) calloc(1,sizeof(mesh_t));
  memcpy(mesh,baseElliptic->mesh,sizeof(mesh_t));

  elliptic->mesh = mesh;

  setupAide options = elliptic->options;

  switch(elliptic->elementType){
    case TRIANGLES:
      meshLoadReferenceNodesTri2D(mesh, Nc);
      meshPhysicalNodesTri2D(mesh);
      break;
    case QUADRILATERALS:
      meshLoadReferenceNodesQuad2D(mesh, Nc);
      meshPhysicalNodesQuad2D(mesh);
      meshGeometricFactorsQuad2D(mesh);
      break;
    case TETRAHEDRA:
      meshLoadReferenceNodesTet3D(mesh, Nc);
      meshPhysicalNodesTet3D(mesh);
      break;
    case HEXAHEDRA:
      meshLoadReferenceNodesHex3D(mesh, Nc);
      meshPhysicalNodesHex3D(mesh);
      meshGeometricFactorsHex3D(mesh);
      break;
  }


  // create halo extension for x,y arrays
  dlong totalHaloNodes = mesh->totalHaloPairs*mesh->Np;
  dlong localNodes     = mesh->Nelements*mesh->Np;
  // temporary send buffer
  dfloat *sendBuffer = (dfloat*) calloc(totalHaloNodes, sizeof(dfloat));

  // extend x,y arrays to hold coordinates of node coordinates of elements in halo
  mesh->x = (dfloat*) realloc(mesh->x, (localNodes+totalHaloNodes)*sizeof(dfloat));
  mesh->y = (dfloat*) realloc(mesh->y, (localNodes+totalHaloNodes)*sizeof(dfloat));
  meshHaloExchange(mesh, mesh->Np*sizeof(dfloat), mesh->x, sendBuffer, mesh->x + localNodes);
  meshHaloExchange(mesh, mesh->Np*sizeof(dfloat), mesh->y, sendBuffer, mesh->y + localNodes);

  if (elliptic->dim==3) {
    mesh->z = (dfloat*) realloc(mesh->z, (localNodes+totalHaloNodes)*sizeof(dfloat));
    meshHaloExchange(mesh, mesh->Np*sizeof(dfloat), mesh->z, sendBuffer, mesh->z + localNodes);
  }

  switch(elliptic->elementType){
    case TRIANGLES:
      meshConnectFaceNodes2D(mesh);
      break;
    case QUADRILATERALS:
      meshConnectFaceNodes2D(mesh);
      meshSurfaceGeometricFactorsQuad2D(mesh);
      break;
    case TETRAHEDRA:
      meshConnectFaceNodes3D(mesh);
      break;
    case HEXAHEDRA:
      meshConnectFaceNodes3D(mesh);
      meshSurfaceGeometricFactorsHex3D(mesh);
      break;
  }

  // global nodes
  meshParallelConnectNodes(mesh);

  //dont need these once vmap is made
  free(mesh->x);
  free(mesh->y);
  if (elliptic->dim==3) {
    free(mesh->z);
  }
  free(sendBuffer);

  dlong Ntotal = mesh->Np*mesh->Nelements;
  dlong Nblock = (Ntotal+blockSize-1)/blockSize;

  elliptic->Nblock = Nblock;

  if (elliptic->elementType==TRIANGLES) {

    // build Dr, Ds, LIFT transposes
    dfloat *DrT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    dfloat *DsT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        DrT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
        DsT[n+m*mesh->Np] = mesh->Ds[n*mesh->Np+m];
      }
    }

    // build Dr, Ds transposes
    dfloat *DrsT = (dfloat*) calloc(2*mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        DrsT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
        DrsT[n+m*mesh->Np+mesh->Np*mesh->Np] = mesh->Ds[n*mesh->Np+m];
      }
    }

    dfloat *LIFTT = (dfloat*) calloc(mesh->Np*mesh->Nfaces*mesh->Nfp, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Nfaces*mesh->Nfp;++m){
        LIFTT[n+m*mesh->Np] = mesh->LIFT[n*mesh->Nfp*mesh->Nfaces+m];
      }
    }

    //build element stiffness matrices
    dfloat *SrrT, *SrsT, *SsrT, *SssT;
    mesh->Srr = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Srs = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Ssr = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Sss = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    for (int n=0;n<mesh->Np;n++) {
      for (int m=0;m<mesh->Np;m++) {
        for (int k=0;k<mesh->Np;k++) {
          for (int l=0;l<mesh->Np;l++) {
            mesh->Srr[m+n*mesh->Np] += mesh->Dr[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dr[m+k*mesh->Np];
            mesh->Srs[m+n*mesh->Np] += mesh->Dr[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Ds[m+k*mesh->Np];
            mesh->Ssr[m+n*mesh->Np] += mesh->Ds[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dr[m+k*mesh->Np];
            mesh->Sss[m+n*mesh->Np] += mesh->Ds[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Ds[m+k*mesh->Np];
          }
        } 
      }
    }
    SrrT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    SrsT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    SsrT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    SssT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    for (int n=0;n<mesh->Np;n++) {
      for (int m=0;m<mesh->Np;m++) {  
        SrrT[m+n*mesh->Np] = mesh->Srr[n+m*mesh->Np];
        SrsT[m+n*mesh->Np] = mesh->Srs[n+m*mesh->Np];
        SsrT[m+n*mesh->Np] = mesh->Ssr[n+m*mesh->Np];
        SssT[m+n*mesh->Np] = mesh->Sss[n+m*mesh->Np];
      }
    }

    dfloat *ST = (dfloat*) calloc(3*mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        ST[n+m*mesh->Np+0*mesh->Np*mesh->Np] = mesh->Srr[n*mesh->Np+m];
        ST[n+m*mesh->Np+1*mesh->Np*mesh->Np] = mesh->Srs[n*mesh->Np+m]+mesh->Ssr[n*mesh->Np+m];
        ST[n+m*mesh->Np+2*mesh->Np*mesh->Np] = mesh->Sss[n*mesh->Np+m];
      }
    }

    // deriv operators: transpose from row major to column major
    int *D1ids = (int*) calloc(mesh->Np*3,sizeof(int));
    int *D2ids = (int*) calloc(mesh->Np*3,sizeof(int));
    int *D3ids = (int*) calloc(mesh->Np*3,sizeof(int));
    dfloat *Dvals = (dfloat*) calloc(mesh->Np*3,sizeof(dfloat));

    dfloat *VBq = (dfloat*) calloc(mesh->Np*mesh->cubNp,sizeof(dfloat));
    dfloat *PBq = (dfloat*) calloc(mesh->Np*mesh->cubNp,sizeof(dfloat));

    dfloat *L0vals = (dfloat*) calloc(mesh->Nfp*3,sizeof(dfloat)); // tridiag
    int *ELids = (int*) calloc(1+mesh->Np*mesh->max_EL_nnz,sizeof(int));
    dfloat *ELvals = (dfloat*) calloc(1+mesh->Np*mesh->max_EL_nnz,sizeof(dfloat));


    for (int i = 0; i < mesh->Np; ++i){
      for (int j = 0; j < 3; ++j){
        D1ids[i+j*mesh->Np] = mesh->D1ids[j+i*3];
        D2ids[i+j*mesh->Np] = mesh->D2ids[j+i*3];
        D3ids[i+j*mesh->Np] = mesh->D3ids[j+i*3];
        Dvals[i+j*mesh->Np] = mesh->Dvals[j+i*3];
      }
    }

    for (int i = 0; i < mesh->cubNp; ++i){
      for (int j = 0; j < mesh->Np; ++j){
        VBq[i+j*mesh->cubNp] = mesh->VBq[j+i*mesh->Np];
        PBq[j+i*mesh->Np] = mesh->PBq[i+j*mesh->cubNp];
      }
    }


    for (int i = 0; i < mesh->Nfp; ++i){
      for (int j = 0; j < 3; ++j){
        L0vals[i+j*mesh->Nfp] = mesh->L0vals[j+i*3];
      }
    }

    for (int i = 0; i < mesh->Np; ++i){
      for (int j = 0; j < mesh->max_EL_nnz; ++j){
        ELids[i + j*mesh->Np] = mesh->ELids[j+i*mesh->max_EL_nnz];
        ELvals[i + j*mesh->Np] = mesh->ELvals[j+i*mesh->max_EL_nnz];
      }
    }

    //BB mass matrix
    mesh->BBMM = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    for (int n = 0; n < mesh->Np; ++n){
      for (int m = 0; m < mesh->Np; ++m){
        for (int i = 0; i < mesh->Np; ++i){
          for (int j = 0; j < mesh->Np; ++j){
            mesh->BBMM[n+m*mesh->Np] += mesh->VB[m+j*mesh->Np]*mesh->MM[i+j*mesh->Np]*mesh->VB[n+i*mesh->Np];
          }
        }
      }
    }

    mesh->o_Dr = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
             mesh->Dr);

    mesh->o_Ds = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
             mesh->Ds);

    mesh->o_DrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
              DrT);

    mesh->o_DsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
              DsT);

    mesh->o_Dmatrices = mesh->device.malloc(2*mesh->Np*mesh->Np*sizeof(dfloat), DrsT);

    mesh->o_LIFT =
      mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
        mesh->LIFT);

    mesh->o_LIFTT =
      mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
        LIFTT);

    

    mesh->o_SrrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SrrT);
    mesh->o_SrsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SrsT);
    mesh->o_SsrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SsrT);
    mesh->o_SssT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SssT);

    mesh->o_Smatrices = mesh->device.malloc(3*mesh->Np*mesh->Np*sizeof(dfloat), ST);

    mesh->o_D1ids = mesh->device.malloc(mesh->Np*3*sizeof(int),D1ids);
    mesh->o_D2ids = mesh->device.malloc(mesh->Np*3*sizeof(int),D2ids);
    mesh->o_D3ids = mesh->device.malloc(mesh->Np*3*sizeof(int),D3ids);
    mesh->o_Dvals = mesh->device.malloc(mesh->Np*3*sizeof(dfloat),Dvals);

    mesh->o_BBMM = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),mesh->BBMM);

    mesh->o_VBq = mesh->device.malloc(mesh->Np*mesh->cubNp*sizeof(dfloat),VBq);
    mesh->o_PBq = mesh->device.malloc(mesh->Np*mesh->cubNp*sizeof(dfloat),PBq);

    mesh->o_L0vals = mesh->device.malloc(mesh->Nfp*3*sizeof(dfloat),L0vals);
    mesh->o_ELids =
      mesh->device.malloc(mesh->Np*mesh->max_EL_nnz*sizeof(int),ELids);
    mesh->o_ELvals =
      mesh->device.malloc(mesh->Np*mesh->max_EL_nnz*sizeof(dfloat),ELvals);

    free(DrT); free(DsT); free(LIFTT);
    free(SrrT); free(SrsT); free(SsrT); free(SssT);
    free(D1ids); free(D2ids); free(D3ids); free(Dvals);

    free(VBq); free(PBq);
    free(L0vals); free(ELids ); free(ELvals);

  } else if (elliptic->elementType==QUADRILATERALS) {

    //lumped mass matrix
    mesh->MM = (dfloat *) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    for (int j=0;j<mesh->Nq;j++) {
      for (int i=0;i<mesh->Nq;i++) {
        int n = i+j*mesh->Nq;
        mesh->MM[n+n*mesh->Np] = mesh->gllw[i]*mesh->gllw[j];
      }
    }

    mesh->o_D = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D);
    mesh->o_Dmatrices = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D);
    mesh->o_Smatrices = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D); //dummy

    mesh->o_vgeo =
      mesh->device.malloc(mesh->Nelements*mesh->Nvgeo*mesh->Np*sizeof(dfloat),
        mesh->vgeo);
    mesh->o_sgeo =
      mesh->device.malloc(mesh->Nelements*mesh->Nfaces*mesh->Nfp*mesh->Nsgeo*sizeof(dfloat),
        mesh->sgeo);
    mesh->o_ggeo =
      mesh->device.malloc(mesh->Nelements*mesh->Np*mesh->Nggeo*sizeof(dfloat),
        mesh->ggeo);
  
  } else if (elliptic->elementType==TETRAHEDRA) {

    // build Dr, Ds, LIFT transposes
    dfloat *DrT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    dfloat *DsT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    dfloat *DtT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        DrT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
        DsT[n+m*mesh->Np] = mesh->Ds[n*mesh->Np+m];
        DtT[n+m*mesh->Np] = mesh->Dt[n*mesh->Np+m];
      }
    }

    // build Dr, Ds transposes
    dfloat *DrstT = (dfloat*) calloc(3*mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        DrstT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
        DrstT[n+m*mesh->Np+mesh->Np*mesh->Np] = mesh->Ds[n*mesh->Np+m];
        DrstT[n+m*mesh->Np+2*mesh->Np*mesh->Np] = mesh->Dt[n*mesh->Np+m];
      }
    }

    dfloat *LIFTT = (dfloat*) calloc(mesh->Np*mesh->Nfaces*mesh->Nfp, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Nfaces*mesh->Nfp;++m){
        LIFTT[n+m*mesh->Np] = mesh->LIFT[n*mesh->Nfp*mesh->Nfaces+m];
      }
    }

    //build element stiffness matrices
    mesh->Srr = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Srs = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Srt = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Ssr = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Sss = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Sst = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Str = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Sts = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    mesh->Stt = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    for (int n=0;n<mesh->Np;n++) {
      for (int m=0;m<mesh->Np;m++) {
        for (int k=0;k<mesh->Np;k++) {
          for (int l=0;l<mesh->Np;l++) {
            mesh->Srr[m+n*mesh->Np] += mesh->Dr[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dr[m+k*mesh->Np];
            mesh->Srs[m+n*mesh->Np] += mesh->Dr[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Ds[m+k*mesh->Np];
            mesh->Srt[m+n*mesh->Np] += mesh->Dr[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dt[m+k*mesh->Np];
            mesh->Ssr[m+n*mesh->Np] += mesh->Ds[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dr[m+k*mesh->Np];
            mesh->Sss[m+n*mesh->Np] += mesh->Ds[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Ds[m+k*mesh->Np];
            mesh->Sst[m+n*mesh->Np] += mesh->Ds[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dt[m+k*mesh->Np];
            mesh->Str[m+n*mesh->Np] += mesh->Dt[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dr[m+k*mesh->Np];
            mesh->Sts[m+n*mesh->Np] += mesh->Dt[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Ds[m+k*mesh->Np];
            mesh->Stt[m+n*mesh->Np] += mesh->Dt[n+l*mesh->Np]*mesh->MM[k+l*mesh->Np]*mesh->Dt[m+k*mesh->Np];
          }
        }
      }
    }
    dfloat *SrrT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SrsT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SrtT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SsrT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SssT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SstT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *StrT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *StsT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    dfloat *SttT = (dfloat *) calloc(mesh->Np*mesh->Np,sizeof(dfloat));
    for (int n=0;n<mesh->Np;n++) {
      for (int m=0;m<mesh->Np;m++) {
        SrrT[m+n*mesh->Np] = mesh->Srr[n+m*mesh->Np];
        SrsT[m+n*mesh->Np] = mesh->Srs[n+m*mesh->Np]+mesh->Ssr[n+m*mesh->Np];
        SrtT[m+n*mesh->Np] = mesh->Srt[n+m*mesh->Np]+mesh->Str[n+m*mesh->Np];
        SssT[m+n*mesh->Np] = mesh->Sss[n+m*mesh->Np];
        SstT[m+n*mesh->Np] = mesh->Sst[n+m*mesh->Np]+mesh->Sts[n+m*mesh->Np];
        SttT[m+n*mesh->Np] = mesh->Stt[n+m*mesh->Np];
      }
    }
    dfloat *ST = (dfloat*) calloc(6*mesh->Np*mesh->Np, sizeof(dfloat));
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        ST[n+m*mesh->Np+0*mesh->Np*mesh->Np] = mesh->Srr[n*mesh->Np+m];
        ST[n+m*mesh->Np+1*mesh->Np*mesh->Np] = mesh->Srs[n*mesh->Np+m]+mesh->Ssr[n*mesh->Np+m];
        ST[n+m*mesh->Np+2*mesh->Np*mesh->Np] = mesh->Srt[n*mesh->Np+m]+mesh->Str[n*mesh->Np+m];
        ST[n+m*mesh->Np+3*mesh->Np*mesh->Np] = mesh->Sss[n*mesh->Np+m];
        ST[n+m*mesh->Np+4*mesh->Np*mesh->Np] = mesh->Sst[n*mesh->Np+m]+mesh->Sts[n*mesh->Np+m];
        ST[n+m*mesh->Np+5*mesh->Np*mesh->Np] = mesh->Stt[n*mesh->Np+m];
      }
    }

    mesh->o_Dr = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
             mesh->Dr);

    mesh->o_Ds = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
             mesh->Ds);

    mesh->o_Dt = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
             mesh->Dt);

    mesh->o_DrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
              DrT);

    mesh->o_DsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
              DsT);

    mesh->o_DtT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
              DtT);

    mesh->o_Dmatrices = mesh->device.malloc(3*mesh->Np*mesh->Np*sizeof(dfloat), DrstT);

    mesh->o_LIFT =
      mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
        mesh->LIFT);

    mesh->o_LIFTT =
      mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
        LIFTT);

    mesh->o_SrrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SrrT);
    mesh->o_SrsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SrsT);
    mesh->o_SrtT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SrtT);
    mesh->o_SsrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SsrT);
    mesh->o_SssT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SssT);
    mesh->o_SstT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SstT);
    mesh->o_StrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), StrT);
    mesh->o_StsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), StsT);
    mesh->o_SttT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), SttT);

    mesh->o_Smatrices = mesh->device.malloc(6*mesh->Np*mesh->Np*sizeof(dfloat), ST);

    free(DrT); free(DsT); free(DtT); free(LIFTT);
    free(SrrT); free(SrsT); free(SrtT); 
    free(SsrT); free(SssT); free(SstT);
    free(StrT); free(StsT); free(SttT);

  } else if (elliptic->elementType==HEXAHEDRA) {

    //lumped mass matrix
    mesh->MM = (dfloat *) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
    for (int k=0;k<mesh->Nq;k++) {
      for (int j=0;j<mesh->Nq;j++) {
        for (int i=0;i<mesh->Nq;i++) {
          int n = i+j*mesh->Nq+k*mesh->Nq*mesh->Nq;
          mesh->MM[n+n*mesh->Np] = mesh->gllw[i]*mesh->gllw[j]*mesh->gllw[k];
        }
      }
    }

    mesh->o_D = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D);
    mesh->o_Dmatrices = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D);
    mesh->o_Smatrices = mesh->device.malloc(mesh->Nq*mesh->Nq*sizeof(dfloat), mesh->D); //dummy

    mesh->o_vgeo =
      mesh->device.malloc(mesh->Nelements*mesh->Nvgeo*mesh->Np*sizeof(dfloat),
        mesh->vgeo);
    mesh->o_sgeo =
      mesh->device.malloc(mesh->Nelements*mesh->Nfaces*mesh->Nfp*mesh->Nsgeo*sizeof(dfloat),
        mesh->sgeo);
    mesh->o_ggeo =
      mesh->device.malloc(mesh->Nelements*mesh->Np*mesh->Nggeo*sizeof(dfloat),
        mesh->ggeo);

    mesh->o_vmapM =
      mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(dlong),
        mesh->vmapM);

    mesh->o_vmapP =
      mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(dlong),
        mesh->vmapP);

  }


  //fill geometric factors in halo
  if(mesh->totalHaloPairs && (elliptic->elementType==QUADRILATERALS || elliptic->elementType==HEXAHEDRA)){
    dlong Nlocal = mesh->Np*mesh->Nelements;
    dlong Nhalo = mesh->totalHaloPairs*mesh->Np;
    dfloat *vgeoSendBuffer = (dfloat*) calloc(Nhalo*mesh->Nvgeo, sizeof(dfloat));

    // import geometric factors from halo elements
    mesh->vgeo = (dfloat*) realloc(mesh->vgeo, (Nlocal+Nhalo)*mesh->Nvgeo*sizeof(dfloat));

    meshHaloExchange(mesh,
         mesh->Nvgeo*mesh->Np*sizeof(dfloat),
         mesh->vgeo,
         vgeoSendBuffer,
         mesh->vgeo + Nlocal*mesh->Nvgeo);

    mesh->o_vgeo =
      mesh->device.malloc((Nlocal+Nhalo)*mesh->Nvgeo*sizeof(dfloat), mesh->vgeo);
    free(vgeoSendBuffer);
  }

  mesh->o_MM =
      mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
        mesh->MM);

  mesh->o_vmapM =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(int),
      mesh->vmapM);

  mesh->o_vmapP =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(int),
      mesh->vmapP);

  
  //set the normalization constant for the allNeumann Poisson problem on this coarse mesh
  hlong localElements = (hlong) mesh->Nelements;
  hlong totalElements = 0;
  MPI_Allreduce(&localElements, &totalElements, 1, MPI_HLONG, MPI_SUM, MPI_COMM_WORLD);
  elliptic->allNeumannScale = 1.0/sqrt(mesh->Np*totalElements);

  // info for kernel construction
  occa::kernelInfo kernelInfo;

  kernelInfo.addDefine("p_Nfields", mesh->Nfields);
  kernelInfo.addDefine("p_N", mesh->N);
  kernelInfo.addDefine("p_Nq", mesh->N+1);
  kernelInfo.addDefine("p_Np", mesh->Np);
  kernelInfo.addDefine("p_Nfp", mesh->Nfp);
  kernelInfo.addDefine("p_Nfaces", mesh->Nfaces);
  kernelInfo.addDefine("p_NfacesNfp", mesh->Nfp*mesh->Nfaces);
  kernelInfo.addDefine("p_Nvgeo", mesh->Nvgeo);
  kernelInfo.addDefine("p_Nsgeo", mesh->Nsgeo);
  kernelInfo.addDefine("p_Nggeo", mesh->Nggeo);

  kernelInfo.addDefine("p_NXID", NXID);
  kernelInfo.addDefine("p_NYID", NYID);
  kernelInfo.addDefine("p_NZID", NZID);
  kernelInfo.addDefine("p_SJID", SJID);
  kernelInfo.addDefine("p_IJID", IJID);
  kernelInfo.addDefine("p_WSJID", WSJID);
  kernelInfo.addDefine("p_IHID", IHID);

  kernelInfo.addDefine("p_max_EL_nnz", mesh->max_EL_nnz); // for Bernstein Bezier lift

  kernelInfo.addDefine("p_cubNp", mesh->cubNp);
  kernelInfo.addDefine("p_intNfp", mesh->intNfp);
  kernelInfo.addDefine("p_intNfpNfaces", mesh->intNfp*mesh->Nfaces);

  if(sizeof(dfloat)==4){
    kernelInfo.addDefine("dfloat","float");
    kernelInfo.addDefine("dfloat4","float4");
    kernelInfo.addDefine("dfloat8","float8");
  }
  if(sizeof(dfloat)==8){
    kernelInfo.addDefine("dfloat","double");
    kernelInfo.addDefine("dfloat4","double4");
    kernelInfo.addDefine("dfloat8","double8");
  }

  if(sizeof(dlong)==4){
    kernelInfo.addDefine("dlong","int");
  }
  if(sizeof(dlong)==8){
    kernelInfo.addDefine("dlong","long long int");
  }

  if(mesh->device.mode()=="CUDA"){ // add backend compiler optimization for CUDA
    kernelInfo.addCompilerFlag("--ftz=true");
    kernelInfo.addCompilerFlag("--prec-div=false");
    kernelInfo.addCompilerFlag("--prec-sqrt=false");
    kernelInfo.addCompilerFlag("--use_fast_math");
    kernelInfo.addCompilerFlag("--fmad=true"); // compiler option for cuda
    kernelInfo.addCompilerFlag("-Xptxas -dlcm=ca");
  }

  if(mesh->device.mode()=="Serial")
    kernelInfo.addCompilerFlag("-g");

  kernelInfo.addDefine("p_G00ID", G00ID);
  kernelInfo.addDefine("p_G01ID", G01ID);
  kernelInfo.addDefine("p_G02ID", G02ID);
  kernelInfo.addDefine("p_G11ID", G11ID);
  kernelInfo.addDefine("p_G12ID", G12ID);
  kernelInfo.addDefine("p_G22ID", G22ID);
  kernelInfo.addDefine("p_GWJID", GWJID);


  kernelInfo.addDefine("p_RXID", RXID);
  kernelInfo.addDefine("p_SXID", SXID);
  kernelInfo.addDefine("p_TXID", TXID);

  kernelInfo.addDefine("p_RYID", RYID);
  kernelInfo.addDefine("p_SYID", SYID);
  kernelInfo.addDefine("p_TYID", TYID);

  kernelInfo.addDefine("p_RZID", RZID);
  kernelInfo.addDefine("p_SZID", SZID);
  kernelInfo.addDefine("p_TZID", TZID);

  kernelInfo.addDefine("p_JID", JID);
  kernelInfo.addDefine("p_JWID", JWID);


  kernelInfo.addParserFlag("automate-add-barriers", "disabled");

  // set kernel name suffix
  char *suffix;
  
  if(elliptic->elementType==TRIANGLES)
    suffix = strdup("Tri2D");
  if(elliptic->elementType==QUADRILATERALS)
    suffix = strdup("Quad2D");
  if(elliptic->elementType==TETRAHEDRA)
    suffix = strdup("Tet3D");
  if(elliptic->elementType==HEXAHEDRA)
    suffix = strdup("Hex3D");

  char fileName[BUFSIZ], kernelName[BUFSIZ];

  for (int r=0;r<size;r++) {
    if (r==rank) {
      kernelInfo.addDefine("p_blockSize", blockSize);

      // add custom defines
      kernelInfo.addDefine("p_NpP", (mesh->Np+mesh->Nfp*mesh->Nfaces));
      kernelInfo.addDefine("p_Nverts", mesh->Nverts);

      int Nmax = mymax(mesh->Np, mesh->Nfaces*mesh->Nfp);
      kernelInfo.addDefine("p_Nmax", Nmax);

      int maxNodes = mymax(mesh->Np, (mesh->Nfp*mesh->Nfaces));
      kernelInfo.addDefine("p_maxNodes", maxNodes);

      int NblockV = maxNthreads/mesh->Np; // works for CUDA
      kernelInfo.addDefine("p_NblockV", NblockV);

      int one = 1; //set to one for now. TODO: try optimizing over these
      kernelInfo.addDefine("p_NnodesV", one);

      int NblockS = maxNthreads/maxNodes; // works for CUDA
      kernelInfo.addDefine("p_NblockS", NblockS);

      int NblockP = maxNthreads/(4*mesh->Np); // get close to maxNthreads threads
      kernelInfo.addDefine("p_NblockP", NblockP);

      int NblockG;
      if(mesh->Np<=32) NblockG = ( 32/mesh->Np );
      else NblockG = maxNthreads/mesh->Np;
      kernelInfo.addDefine("p_NblockG", NblockG);

      //add standard boundary functions
      char *boundaryHeaderFileName;
      if (elliptic->dim==2)
        boundaryHeaderFileName = strdup(DELLIPTIC "/data/ellipticBoundary2D.h");
      else if (elliptic->dim==3)
        boundaryHeaderFileName = strdup(DELLIPTIC "/data/ellipticBoundary3D.h");
      kernelInfo.addInclude(boundaryHeaderFileName);

      sprintf(fileName, DELLIPTIC "/okl/ellipticAx%s.okl", suffix);
      sprintf(kernelName, "ellipticAx%s", suffix);
      elliptic->AxKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

      sprintf(kernelName, "ellipticPartialAx%s", suffix);
      elliptic->partialAxKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);


      if (options.compareArgs("BASIS", "BERN")) {

        sprintf(fileName, DELLIPTIC "/okl/ellipticGradientBB%s.okl", suffix);
        sprintf(kernelName, "ellipticGradientBB%s", suffix);

        elliptic->gradientKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

        sprintf(kernelName, "ellipticPartialGradientBB%s", suffix);
        elliptic->partialGradientKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);
      
        sprintf(fileName, DELLIPTIC "/okl/ellipticAxIpdgBB%s.okl", suffix);
        sprintf(kernelName, "ellipticAxIpdgBB%s", suffix);
        elliptic->ipdgKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

        sprintf(kernelName, "ellipticPartialAxIpdgBB%s", suffix);
        elliptic->partialIpdgKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);
          
      } else if (options.compareArgs("BASIS", "NODAL")) {

        sprintf(fileName, DELLIPTIC "/okl/ellipticGradient%s.okl", suffix);
        sprintf(kernelName, "ellipticGradient%s", suffix);

        elliptic->gradientKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

        sprintf(kernelName, "ellipticPartialGradient%s", suffix);
        elliptic->partialGradientKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

        sprintf(fileName, DELLIPTIC "/okl/ellipticAxIpdg%s.okl", suffix);
        sprintf(kernelName, "ellipticAxIpdg%s", suffix);
        elliptic->ipdgKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

        sprintf(kernelName, "ellipticPartialAxIpdg%s", suffix);
        elliptic->partialIpdgKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);
      }
    }
  }

  //new precon struct
  elliptic->precon = (precon_t *) calloc(1,sizeof(precon_t));

  for (int r=0;r<size;r++) {
    if (r==rank) {
      sprintf(fileName, DELLIPTIC "/okl/ellipticBlockJacobiPrecon.okl");
      sprintf(kernelName, "ellipticBlockJacobiPrecon");
      elliptic->precon->blockJacobiKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

      sprintf(kernelName, "ellipticPartialBlockJacobiPrecon");
      elliptic->precon->partialblockJacobiKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

      sprintf(fileName, DELLIPTIC "/okl/ellipticPatchSolver.okl");
      sprintf(kernelName, "ellipticApproxBlockJacobiSolver");
      elliptic->precon->approxBlockJacobiSolverKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

      //sizes for the coarsen and prolongation kernels. degree NFine to degree N
      int NqFine   = (Nf+1);
      int NqCoarse = (Nc+1);
      kernelInfo.addDefine("p_NqFine", Nf+1);
      kernelInfo.addDefine("p_NqCoarse", Nc+1);

      int NpFine, NpCoarse;
      switch(elliptic->elementType){
        case TRIANGLES:
          NpFine   = (Nf+1)*(Nf+2)/2;
          NpCoarse = (Nc+1)*(Nc+2)/2;
          break;
        case QUADRILATERALS:
          NpFine   = (Nf+1)*(Nf+1);
          NpCoarse = (Nc+1)*(Nc+1);
          break;
        case TETRAHEDRA:
          NpFine   = (Nf+1)*(Nf+2)*(Nf+3)/6;
          NpCoarse = (Nc+1)*(Nc+2)*(Nc+3)/6;
          break;
        case HEXAHEDRA:
          NpFine   = (Nf+1)*(Nf+1)*(Nf+1);
          NpCoarse = (Nc+1)*(Nc+1)*(Nc+1);
          break;
      }
      kernelInfo.addDefine("p_NpFine", NpFine);
      kernelInfo.addDefine("p_NpCoarse", NpCoarse);

      int NblockVFine = maxNthreads/NpFine; 
      int NblockVCoarse = maxNthreads/NpCoarse; 
      kernelInfo.addDefine("p_NblockVFine", NblockVFine);
      kernelInfo.addDefine("p_NblockVCoarse", NblockVCoarse);

      sprintf(fileName, DELLIPTIC "/okl/ellipticPreconCoarsen%s.okl", suffix);
      sprintf(kernelName, "ellipticPreconCoarsen%s", suffix);
      elliptic->precon->coarsenKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);

      sprintf(fileName, DELLIPTIC "/okl/ellipticPreconProlongate%s.okl", suffix);
      sprintf(kernelName, "ellipticPreconProlongate%s", suffix);
      elliptic->precon->prolongateKernel = mesh->device.buildKernelFromSource(fileName,kernelName,kernelInfo);
    }
  }

  //on host gather-scatter
  int verbose = options.compareArgs("VERBOSE", "TRUE") ? 1:0;
  mesh->hostGsh = gsParallelGatherScatterSetup(mesh->Nelements*mesh->Np, mesh->globalIds, verbose);

  // set up separate gather scatter infrastructure for halo and non halo nodes
  ellipticParallelGatherScatterSetup(elliptic);

  //make a node-wise bc flag using the gsop (prioritize Dirichlet boundaries over Neumann)
  elliptic->mapB = (int *) calloc(mesh->Nelements*mesh->Np,sizeof(int));
  for (dlong e=0;e<mesh->Nelements;e++) {
    for (int n=0;n<mesh->Np;n++) elliptic->mapB[n+e*mesh->Np] = 1E9;
    for (int f=0;f<mesh->Nfaces;f++) {
      int bc = mesh->EToB[f+e*mesh->Nfaces];
      if (bc>0) {
        for (int n=0;n<mesh->Nfp;n++) {
          int BCFlag = elliptic->BCType[bc];
          int fid = mesh->faceNodes[n+f*mesh->Nfp];
          elliptic->mapB[fid+e*mesh->Np] = mymin(BCFlag,elliptic->mapB[fid+e*mesh->Np]);
        }
      }
    }
  }
  gsParallelGatherScatter(mesh->hostGsh, elliptic->mapB, "int", "min"); 

  //use the bc flags to find masked ids
  elliptic->Nmasked = 0;
  for (dlong n=0;n<mesh->Nelements*mesh->Np;n++) {
    if (elliptic->mapB[n] == 1E9) {
      elliptic->mapB[n] = 0.;
    } else if (elliptic->mapB[n] == 1) { //Dirichlet boundary
      elliptic->Nmasked++;
    }
  }
  elliptic->o_mapB = mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(int), elliptic->mapB);
  
  elliptic->maskIds = (dlong *) calloc(elliptic->Nmasked, sizeof(dlong));
  elliptic->Nmasked =0; //reset
  for (dlong n=0;n<mesh->Nelements*mesh->Np;n++) {
    if (elliptic->mapB[n] == 1) elliptic->maskIds[elliptic->Nmasked++] = n;
  }
  if (elliptic->Nmasked) elliptic->o_maskIds = mesh->device.malloc(elliptic->Nmasked*sizeof(dlong), elliptic->maskIds);

  return elliptic;
}
