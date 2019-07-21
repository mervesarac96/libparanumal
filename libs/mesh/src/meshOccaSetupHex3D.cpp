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

#include "mesh.hpp"
#include "mesh3D.hpp"

void meshHex3D::OccaSetup(){

  this->mesh3D::OccaSetup();

  //lumped mass matrix
  MM = (dfloat *) calloc(Np*Np, sizeof(dfloat));
  for (int k=0;k<Nq;k++) {
    for (int j=0;j<Nq;j++) {
      for (int i=0;i<Nq;i++) {
        int n = i+j*Nq+k*Nq*Nq;
        MM[n+n*Np] = gllw[i]*gllw[j]*gllw[k];
      }
    }
  }

  //build inverse of mass matrix
  invMM = (dfloat *) calloc(Np*Np,sizeof(dfloat));
  for (int n=0;n<Np*Np;n++)
    invMM[n] = MM[n];
  matrixInverse(Np,invMM);

  LIFT = (dfloat*) calloc(Np*Nfaces*Nfp, sizeof(dfloat));

  dfloat *cubDWT = (dfloat*) calloc(cubNq*cubNq, sizeof(dfloat));
  dfloat *cubProjectT = (dfloat*) calloc(cubNq*Nq, sizeof(dfloat));
  dfloat *cubInterpT = (dfloat*) calloc(cubNq*Nq, sizeof(dfloat));
  for(int n=0;n<Nq;++n){
    for(int m=0;m<cubNq;++m){
      cubProjectT[n+m*Nq] = cubProject[n*cubNq+m];
      cubInterpT[m+n*cubNq] = cubInterp[m*Nq+n];
    }
  }
  for(int n=0;n<cubNq;++n){
    for(int m=0;m<cubNq;++m){
      cubDWT[n+m*cubNq] = cubDW[n*cubNq+m];
    }
  }

  o_LIFTT =
    device.malloc(1*sizeof(dfloat)); // dummy

  //    reportMemoryUsage(device, "meshOccaSetup3D: before intX ");

  intx = (dfloat*) calloc(Nelements*Nfaces*cubNfp, sizeof(dfloat));
  inty = (dfloat*) calloc(Nelements*Nfaces*cubNfp, sizeof(dfloat));
  intz = (dfloat*) calloc(Nelements*Nfaces*cubNfp, sizeof(dfloat));

  dfloat *ix = (dfloat *) calloc(cubNq*Nq,sizeof(dfloat));
  dfloat *iy = (dfloat *) calloc(cubNq*Nq,sizeof(dfloat));
  dfloat *iz = (dfloat *) calloc(cubNq*Nq,sizeof(dfloat));
  for(dlong e=0;e<Nelements;++e){
    for(int f=0;f<Nfaces;++f){
      //interpolate in i
      for(int ny=0;ny<Nq;++ny){
        for(int nx=0;nx<cubNq;++nx){
          ix[nx+cubNq*ny] = 0;
          iy[nx+cubNq*ny] = 0;
          iz[nx+cubNq*ny] = 0;

          for(int m=0;m<Nq;++m){
            dlong vid = m+ny*Nq+f*Nfp+e*Nfp*Nfaces;
            dlong idM = vmapM[vid];

            dfloat xm = x[idM];
            dfloat ym = y[idM];
            dfloat zm = z[idM];

            dfloat Inm = cubInterp[m+nx*Nq];
            ix[nx+cubNq*ny] += Inm*xm;
            iy[nx+cubNq*ny] += Inm*ym;
            iz[nx+cubNq*ny] += Inm*zm;
          }
        }
      }

      //interpolate in j and store
      for(int ny=0;ny<cubNq;++ny){
        for(int nx=0;nx<cubNq;++nx){
          dfloat xn=0.0, yn=0.0, zn=0.0;

          for(int m=0;m<Nq;++m){
            dfloat xm = ix[nx + m*cubNq];
            dfloat ym = iy[nx + m*cubNq];
            dfloat zm = iz[nx + m*cubNq];

            dfloat Inm = cubInterp[m+ny*Nq];
            xn += Inm*xm;
            yn += Inm*ym;
            zn += Inm*zm;
          }

          dlong id = nx + ny*cubNq + f*cubNfp + e*Nfaces*cubNfp;
          intx[id] = xn;
          inty[id] = yn;
          intz[id] = zn;
        }
      }
    }
  }
  free(ix); free(iy); free(iz);

  // build trilinear geometric factors for hexes
  if(settings.compareSetting("ELEMENT MAP", "AFFINE")){
    // pack gllz, gllw, and elementwise EXYZ
    hlong Nxyz = Nelements*dim*Nverts;
    EXYZ  = (dfloat*) calloc(Nxyz, sizeof(dfloat));
    gllzw = (dfloat*) calloc(2*Nq, sizeof(dfloat));

    int sk = 0;
    for(int n=0;n<Nq;++n)
      gllzw[sk++] = gllz[n];
    for(int n=0;n<Nq;++n)
      gllzw[sk++] = gllw[n];

    sk = 0;
    for(hlong e=0;e<Nelements;++e){
      for(int v=0;v<Nverts;++v)
        EXYZ[sk++] = EX[e*Nverts+v];
      for(int v=0;v<Nverts;++v)
        EXYZ[sk++] = EY[e*Nverts+v];
      for(int v=0;v<Nverts;++v)
        EXYZ[sk++] = EZ[e*Nverts+v];
    }

    // nodewise ggeo with element coordinates and gauss node info
    o_EXYZ  = device.malloc(Nxyz*sizeof(dfloat), EXYZ);
    o_gllzw = device.malloc(2*Nq*sizeof(dfloat), gllzw);
  }

  ggeoNoJW = (dfloat*) calloc(Np*Nelements*6,sizeof(dfloat));
  for(int e=0;e<Nelements;++e){
    for(int n=0;n<Np;++n){
      ggeoNoJW[e*Np*6 + n + 0*Np] = ggeo[e*Np*Nggeo + n + G00ID*Np];
      ggeoNoJW[e*Np*6 + n + 1*Np] = ggeo[e*Np*Nggeo + n + G01ID*Np];
      ggeoNoJW[e*Np*6 + n + 2*Np] = ggeo[e*Np*Nggeo + n + G02ID*Np];
      ggeoNoJW[e*Np*6 + n + 3*Np] = ggeo[e*Np*Nggeo + n + G11ID*Np];
      ggeoNoJW[e*Np*6 + n + 4*Np] = ggeo[e*Np*Nggeo + n + G12ID*Np];
      ggeoNoJW[e*Np*6 + n + 5*Np] = ggeo[e*Np*Nggeo + n + G22ID*Np];
    }
  }
  o_ggeoNoJW = device.malloc(Np*Nelements*6*sizeof(dfloat), ggeoNoJW);


  o_MM =
    device.malloc(Np*Np*sizeof(dfloat),
      MM); //dummy

  o_D = device.malloc(Nq*Nq*sizeof(dfloat), D);

  o_Dmatrices = device.malloc(Nq*Nq*sizeof(dfloat), D);

  dfloat *DT = (dfloat*) calloc(Nq*Nq,sizeof(dfloat));
  for(int j=0;j<Nq;++j){
    for(int i=0;i<Nq;++i){
      DT[i*Nq+j] = D[j*Nq+i];
    }
  }

  o_Smatrices = device.malloc(Nq*Nq*sizeof(dfloat), DT); //dummy

  //    reportMemoryUsage(device, "meshOccaSetup3D: before geofactors ");

  o_MM = device.malloc(Np*Np*sizeof(dfloat), MM);

  o_sMT = device.malloc(1*sizeof(dfloat)); //dummy

  o_vgeo =
    device.malloc((Nelements+totalHaloPairs)*Np*Nvgeo*sizeof(dfloat),
                        vgeo);

  o_sgeo =
    device.malloc(Nelements*Nfaces*Nfp*Nsgeo*sizeof(dfloat),
                        sgeo);

  //    reportMemoryUsage(device, "meshOccaSetup3D: before vgeo,sgeo ");

  o_ggeo =
    device.malloc(Nelements*Np*Nggeo*sizeof(dfloat),
      ggeo);

  o_cubvgeo =
    device.malloc(Nelements*Nvgeo*cubNp*sizeof(dfloat),
        cubvgeo);

  o_cubsgeo =
    device.malloc(Nelements*Nfaces*cubNfp*Nsgeo*sizeof(dfloat),
        cubsgeo);

  o_cubggeo =
    device.malloc(Nelements*Nggeo*cubNp*sizeof(dfloat),
        cubggeo);

  o_cubInterpT =
    device.malloc(Nq*cubNq*sizeof(dfloat),
        cubInterpT);

  o_cubProjectT =
    device.malloc(Nq*cubNq*sizeof(dfloat),
		  cubProjectT);

  o_cubDWT =
    device.malloc(cubNq*cubNq*sizeof(dfloat),
		  cubDWT);

  o_cubD =
    device.malloc(cubNq*cubNq*sizeof(dfloat),
		  cubD);

  o_cubDWmatrices = device.malloc(cubNq*cubNq*sizeof(dfloat), cubDWT);

  //    reportMemoryUsage(device, "meshOccaSetup3D: after geofactors ");

  o_intx =
    device.malloc(Nelements*Nfaces*cubNfp*sizeof(dfloat),
        intx);

  o_inty =
    device.malloc(Nelements*Nfaces*cubNfp*sizeof(dfloat),
        inty);

  o_intz =
    device.malloc(Nelements*Nfaces*cubNfp*sizeof(dfloat),
        intz);

  o_intInterpT = device.malloc(cubNq*Nq*sizeof(dfloat));
  o_intInterpT.copyFrom(o_cubInterpT);

  o_intLIFTT = device.malloc(cubNq*Nq*sizeof(dfloat));
  o_intLIFTT.copyFrom(o_cubProjectT);
}