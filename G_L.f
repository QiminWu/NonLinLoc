


	implicit none
	integer latdeg,londeg,an,mois,jour,hr,min,Iproj
	real latmin,lonmin,X,Y,prof,mag,latitude,longitude,sec
	character*80 nomfic
	character*1 ns,ew,point


        print*,'Projection souhaitee ?'
        print*,'1 : LAMBERT I'
        print*,'2 : LAMBERT II'
        print*,'3 : LAMBERT III'
        print*,'4 : LAMBERT IV'
        print*,'5 : EUROLAMBERT'
        print*,'6 : LAMBERT II ETENDU'
        read(5,*) IProj



	print*,'Nom du Fichier ?'
	read(5,'(a)') nomfic
	open(10,form='formatted',file=nomfic)

1 	read(10,*,end=100) longitude,latitude 


	call InitLamb(Iproj)
	call GeoToLamb(longitude,latitude,X,Y) 


	write(18,999) X,Y
	goto 1

100	continue
999	format(1x,f9.4,2x,f9.4)

	close(10)

	end


	subroutine InitLamb(IProj)

c IProj=1 : LAMBERT I
c IProj=2 : LAMBERT II
c IProj=3 : LAMBERT III
c IProj=4 : LAMBERT IV
c IProj=5 : EUROLAMBERT
c IProj=6 : LAMBERT II ETENDU

	implicit  none

	logical   initialized
	integer*4 IProj
	real*8    C,e,longitude0,LatIso,n,latitude0,pi,R0,XS,YS
	real*8    as(6),es(6),k0s(6),longitude0s(6),latitude0s(6),X0s(6)
	real*8	  Y0s(6)

	common    /lambert/C,e,longitude0,n,pi,XS,YS,initialized

	data as/4*6378249.2,6378388,6378249.2/
	data es/4*.08248325676,.08199188998,.08248325676/
	data k0s/.99987734,.99987742,.99987750,.99994471,.99987752,
     &	         .99987742/
	data longitude0s/6*2.596921296/
	data latitude0s/55,52,49,46.85,2*52/
	data X0s/3*600000,234.358,2*600000/
	data Y0s/3*200000,185861.369,2*2200000/

	if (IProj.le.0.or.IProj.gt.6) then
	  print *,'InitLamb: parametre de projection invalide'
	  stop
	endif
	pi=4*atan(1d0)
	e=es(IProj)
	longitude0=longitude0s(IProj)*pi/200
	latitude0=latitude0s(IProj)*pi/200
	R0=k0s(IProj)*as(IProj)/sqrt(1-e*sin(latitude0)*e*
     &	   sin(latitude0))/tan(latitude0)
	LatIso=log(tan(pi/4+latitude0/2))
     &	       -e/2*log((1+e*sin(latitude0))/(1-e*sin(latitude0)))
	n=sin(latitude0)
	C=R0*exp(n*LatIso)
	XS=X0s(IProj)
	YS=Y0s(IProj)+R0
	initialized=.true.
	return
	end


	subroutine GeoToLamb(longitude,latitude,X,Y)

c longitude,latitude : longitude et latitude (degres decimaux) REAL*4
c                X,Y : coordonnees Lambert (kilometres)        REAL*4

	implicit none

	logical  initialized
	real*4   latitude,Longitude,X,Y
	real*8   C,e,gamma,LatIso,latitude1,longitude0,longitude1,n
	real*8   pi,R,XS,YS

	common   /lambert/C,e,longitude0,n,pi,XS,YS,initialized

	if (.not.initialized) then
	  print *,'GeoToLamb: appel a InitLamb manquant'
	  stop
	endif
	longitude1=longitude*pi/180
	latitude1=latitude*pi/180
	LatIso=log(tan(pi/4+latitude1/2))
     &	       -e/2*log((1+e*sin(latitude1))/(1-e*sin(latitude1)))
	gamma=n*(longitude1-longitude0)
	R=C*exp(-n*LatIso)
	X=(XS+R*sin(gamma))*.001
	Y=(YS-R*cos(gamma))*.001
	return
	end
