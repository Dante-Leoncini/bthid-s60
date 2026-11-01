# el icono de la app (qgn_menu_bthid.svg; /V3: SVGB, el NVG del SDK de Symbian^3 no se ve en S60 3rd) a \resource\apps\bthidmgr_icon.mif
ifeq (WINS,$(findstring WINS, $(PLATFORM)))
ZDIR=$(EPOCROOT)epoc32\release\$(PLATFORM)\$(CFG)\Z
else
ZDIR=$(EPOCROOT)epoc32\data\z
endif

TARGETDIR=$(ZDIR)\resource\apps
ICONTARGETFILENAME=$(TARGETDIR)\bthidmgr_icon.mif

ICONDIR=.

do_nothing :
	@rem do_nothing

MAKMAKE : do_nothing

BLD : do_nothing

CLEAN :
	del /q /f $(ICONTARGETFILENAME)

LIB : do_nothing

CLEANLIB : do_nothing

RESOURCE : $(ICONTARGETFILENAME)

$(ICONTARGETFILENAME) : $(ICONDIR)\qgn_menu_bthid.svg
	mifconv $(ICONTARGETFILENAME) \
		/V3 /c32 $(ICONDIR)\qgn_menu_bthid.svg

FREEZE : do_nothing

SAVESPACE : do_nothing

RELEASABLES :
	@echo $(ICONTARGETFILENAME)

FINAL : do_nothing
