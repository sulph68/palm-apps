#########################################################################
#       Make Sure Bourne Shell is Used                                  #
#########################################################################
SHELL =		/bin/sh

#########################################################################
#       Application Specific Defines                                    #
#########################################################################
MAIN =          McPhling
MAIN_V =        McPhlingVFS
OBJ_FILES =	$(OBJ_DIR)/code1000.o   \
		$(OBJ_DIR)/code1001.o   \
		$(OBJ_DIR)/code2000.o
OBJ_FILES_5 =	$(MAIN_O_5)             \
		$(OBJ_DIR_5)/code1000.o \
		$(OBJ_DIR_5)/code1001.o \
		$(OBJ_DIR_5)/code2000.o
COMMON =	common
ICON_TEXT =     "McPhling"
ICON_TEXT_V =   "McPhlingVFS"
APP_ID =        McPg
APP_ID_V =      McPv
APP_TYPE =	'HACK'
APP_TYPE_V =	'appl'
APP_TYPE_5 =	'appl'
DIST_FILES =	$(MAIN_PRC)   $(MAIN_PRC_5)   $(README_HTM)
DIST_FILES_P =	$(MAIN_PRC_P) $(MAIN_PRC_5_P) $(README_HTM_P)
DIST_ALL =	*
SDK_VER =	palmos4

#########################################################################
#       The Architecture Name We're Targeting                           #
#########################################################################
ARCH =		m68k-palmos

#########################################################################
#       Specific Tools                                                  #
#########################################################################
CC =		$(ARCH)-gcc
LK =		$(CC)
AR =		$(ARCH)-ar
PILRC =		pilrc
BUILDPRC =	build-prc

#########################################################################
#       Compiler Flags                                                  #
#########################################################################
CCFLAGS =	-Wall -W -O2 -$(SDK_VER)
#CCFLAGS =	-Wall -W -g  -$(SDK_VER) -mdebug-labels
CCFLAGS_NS =	$(CCFLAGS) -nostartfiles
LKFLAGS =	$(CCFLAGS)
LIBS =		-lPalmOSGlue -lcommon -L$(OBJ_DIR)
LIBS_5 =	-lPalmOSGlue -lcommon -L$(OBJ_DIR_5)
BUILDPRCFLAGS =	
PILRCFLAGS =	-q -I $(SRC_DIR)

#########################################################################
#       Some File Management Macros                                     #
#########################################################################
CD =		cd
CP =		cp
DIR =		ls -l
MV =		mv -f
RM =		rm -f
SED =		sed
TOUCH =		touch
ZIP =		zip -j9
ZIP_ALL =	zip -r9

#########################################################################
#       File & Location Defines                                         #
#########################################################################
OBJ_DIR =		obj
OBJ_DIR_5 =		obj5
SRC_DIR =		src

MAIN_C =		$(SRC_DIR)/$(MAIN).c
MAIN_H =		$(SRC_DIR)/$(MAIN).h
MAIN_O_5 =		$(OBJ_DIR_5)/$(MAIN).o
MAIN_RCP =		$(SRC_DIR)/$(MAIN).rcp
MAIN_RCH =		$(SRC_DIR)/$(MAIN).rch
COMMON_C =		$(COMMON).c
COMMON_O =		$(COMMON).o
COMMON_LIB =		lib$(COMMON).a
MAIN_V_C =		$(SRC_DIR)/$(MAIN_V).c
OBJ_MAIN_V =		$(OBJ_DIR)/$(MAIN_V)
OBJ_MAIN_V_5 =		$(OBJ_DIR_5)/$(MAIN_V)
MAIN_V_PRC =		$(OBJ_DIR)/$(MAIN_V).prc
MAIN_V_PRC_5 =		$(OBJ_DIR_5)/$(MAIN_V).prc
OBJ_MAIN_5 =		$(OBJ_DIR_5)/$(MAIN)
DIST_SRC_ZIP =		$(OUT_DIR)/$(MAIN)Source.zip
README_GENERIC_HTM =	$(SRC_DIR)/README_generic.htm
README_TMP =		$(OBJ_DIR)/README.tmp

#=======================================================================#
#	McCollister Branding                                            #
#=======================================================================#
OUT_DIR =		out
REG_LINK_TEXT =		MikeMcCollister.com\/palm\/
REG_LINK =		http:\/\/$(REG_LINK_TEXT)

MAIN_PRC =		$(OUT_DIR)/$(MAIN)4.prc
MAIN_PRC_5 =		$(OUT_DIR)/$(MAIN)5.prc
MAIN_RO =		$(MAIN).ro
OBJ_MAIN_RO =		$(OBJ_DIR)/$(MAIN_RO)
OBJ_MAIN_RO_5 =		$(OBJ_DIR_5)/$(MAIN_RO)
README_HTM =		$(OUT_DIR)/README.htm
DIST_ZIP =		$(OUT_DIR)/$(MAIN).zip

#=======================================================================#
#	PalmGear Branding                                               #
#=======================================================================#
OUT_DIR_P =		$(OUT_DIR)/PalmGear
REG_LINK_TEXT_P =	PalmGear.com
REG_LINK_P =		http:\/\/$(REG_LINK_TEXT_P)\/\?11596

MAIN_P =		$(MAIN)_p
MAIN_PRC_P =		$(OUT_DIR_P)/$(MAIN)4_p.prc
MAIN_PRC_5_P =		$(OUT_DIR_P)/$(MAIN)5_p.prc
MAIN_RO_P =		$(MAIN_P).ro
OBJ_MAIN_RO_P =		$(OBJ_DIR)/$(MAIN_RO_P)
OBJ_MAIN_RO_5_P =	$(OBJ_DIR_5)/$(MAIN_RO_P)
README_HTM_P =		$(OUT_DIR_P)/README.htm
DIST_ZIP_P =		$(OUT_DIR_P)/$(MAIN).zip

#########################################################################
#       Makefile Targets                                                #
#########################################################################
all: $(DIST_FILES) $(DIST_FILES_P)

#=======================================================================#
#       Version 4.x (Extension/Hack)					#
#=======================================================================#
$(OBJ_DIR)/$(COMMON_LIB): $(SRC_DIR)/$(COMMON_C)
	$(RM) $@
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_00 -o $(OBJ_DIR)/$(COMMON)_00.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_01 -o $(OBJ_DIR)/$(COMMON)_01.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_02 -o $(OBJ_DIR)/$(COMMON)_02.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_03 -o $(OBJ_DIR)/$(COMMON)_03.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_04 -o $(OBJ_DIR)/$(COMMON)_04.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_05 -o $(OBJ_DIR)/$(COMMON)_05.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_06 -o $(OBJ_DIR)/$(COMMON)_06.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_07 -o $(OBJ_DIR)/$(COMMON)_07.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_08 -o $(OBJ_DIR)/$(COMMON)_08.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_09 -o $(OBJ_DIR)/$(COMMON)_09.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_10 -o $(OBJ_DIR)/$(COMMON)_10.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_11 -o $(OBJ_DIR)/$(COMMON)_11.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_12 -o $(OBJ_DIR)/$(COMMON)_12.o -c $?
	$(CC) $(CCFLAGS_NS) \
		-DLIBCOMMON_13 -o $(OBJ_DIR)/$(COMMON)_13.o -c $?
	$(AR) q $@ $(OBJ_DIR)/$(COMMON)_??.o
	$(RM) $(OBJ_DIR)/$(COMMON)_??.o

$(OBJ_DIR)/code1000.o: $(SRC_DIR)/code1000.c $(OBJ_DIR)/$(COMMON_LIB)
	$(CC) $(CCFLAGS_NS) \
		-o $@ $(SRC_DIR)/$(*F).c $(LIBS)

$(OBJ_DIR)/code1001.o: $(SRC_DIR)/code1001.c $(OBJ_DIR)/$(COMMON_LIB)
	$(CC) $(CCFLAGS_NS) \
		-o $@ $(SRC_DIR)/$(*F).c $(LIBS)

$(OBJ_DIR)/code2000.o: $(SRC_DIR)/code2000.c $(OBJ_DIR)/$(COMMON_LIB)
	$(CC) $(CCFLAGS_NS) \
		-o $@ $(SRC_DIR)/$(*F).c $(LIBS)

$(MAIN_V_PRC): $(MAIN_V_C)
	$(CC) $(CCFLAGS) \
		$(MAIN_V_C) -o $(OBJ_MAIN_V)
	$(BUILDPRC) $(BUILDPRCFLAGS) --hidden -o $(MAIN_V_PRC) \
		-n $(ICON_TEXT_V) -c $(APP_ID_V) -t $(APP_TYPE_V) \
		$(OBJ_MAIN_V)

#=======================================================================#
#       Version 4.x (McCollister Branding)				#
#=======================================================================#
$(MAIN_PRC): $(OBJ_FILES) $(OBJ_MAIN_RO)
	$(BUILDPRC) --hack $(BUILDPRCFLAGS) -o $@ -n $(ICON_TEXT) \
		-c $(APP_ID) -t $(APP_TYPE) $(OBJ_MAIN_RO) \
		$(OBJ_DIR)/*.o
	$(DIR) $@

$(OBJ_MAIN_RO): $(MAIN_RCP) $(MAIN_H) $(MAIN_V_PRC)
	$(PILRC) -D BRAND_M $(PILRCFLAGS) -ro -o $@ $(MAIN_RCP)

#=======================================================================#
#       Version 4.x (PalmGear Branding)					#
#=======================================================================#
$(MAIN_PRC_P): $(OBJ_FILES) $(OBJ_MAIN_RO_P)
	$(BUILDPRC) --hack $(BUILDPRCFLAGS) -o $@ -n $(ICON_TEXT) \
		-c $(APP_ID) -t $(APP_TYPE) $(OBJ_MAIN_RO_P) \
		$(OBJ_DIR)/*.o
	$(DIR) $@

$(OBJ_MAIN_RO_P): $(MAIN_RCP) $(MAIN_H) $(MAIN_V_PRC)
	$(PILRC) -D BRAND_P $(PILRCFLAGS) -ro -o $@ $(MAIN_RCP)

#=======================================================================#
#       Version 5.x (Stand Alone, OS 5 Only)				#
#=======================================================================#
$(OBJ_DIR_5)/$(COMMON_LIB): $(SRC_DIR)/$(COMMON_C)
	$(RM) $@
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_00 -o $(OBJ_DIR_5)/$(COMMON)_00.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_01 -o $(OBJ_DIR_5)/$(COMMON)_01.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_02 -o $(OBJ_DIR_5)/$(COMMON)_02.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_03 -o $(OBJ_DIR_5)/$(COMMON)_03.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_04 -o $(OBJ_DIR_5)/$(COMMON)_04.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_05 -o $(OBJ_DIR_5)/$(COMMON)_05.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_06 -o $(OBJ_DIR_5)/$(COMMON)_06.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_07 -o $(OBJ_DIR_5)/$(COMMON)_07.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_08 -o $(OBJ_DIR_5)/$(COMMON)_08.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_09 -o $(OBJ_DIR_5)/$(COMMON)_09.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_10 -o $(OBJ_DIR_5)/$(COMMON)_10.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_11 -o $(OBJ_DIR_5)/$(COMMON)_11.o -c $?
	$(CC) $(CCFLAGS_NS) -DSTANDALONE \
		-DLIBCOMMON_12 -o $(OBJ_DIR_5)/$(COMMON)_12.o -c $?
	$(AR) q $@ $(OBJ_DIR_5)/$(COMMON)_??.o
	$(RM) $(OBJ_DIR_5)/$(COMMON)_??.o

$(OBJ_MAIN_5): $(OBJ_DIR_5)/$(COMMON_LIB) $(OBJ_FILES_5)
	$(LK) $(LKFLAGS) \
		$(OBJ_FILES_5) \
		-o $(OBJ_DIR_5)/$(MAIN) $(LIBS_5)

$(MAIN_O_5): $(MAIN_C)
	$(CC) $(CCFLAGS_NS) \
		-DSTANDALONE -c $(SRC_DIR)/$(*F).c -o $@

$(OBJ_DIR_5)/code1000.o: $(SRC_DIR)/code1000.c
	$(CC) $(CCFLAGS_NS) \
		-DSTANDALONE -c $(SRC_DIR)/$(*F).c -o $@

$(OBJ_DIR_5)/code1001.o: $(SRC_DIR)/code1001.c
	$(CC) $(CCFLAGS_NS) \
		-DSTANDALONE -c $(SRC_DIR)/$(*F).c -o $@

$(OBJ_DIR_5)/code2000.o: $(SRC_DIR)/code2000.c
	$(CC) $(CCFLAGS_NS) \
		-DSTANDALONE -c $(SRC_DIR)/$(*F).c -o $@

$(MAIN_V_PRC_5): $(MAIN_V_C)
	$(CC) $(CCFLAGS) -DSTANDALONE $(MAIN_V_C) \
		-o $(OBJ_MAIN_V_5)
	$(BUILDPRC) $(BUILDPRCFLAGS) --hidden -o $(MAIN_V_PRC_5) \
		-n $(ICON_TEXT_V) -c $(APP_ID_V) -t $(APP_TYPE_V) \
		$(OBJ_MAIN_V_5)

#=======================================================================#
#       Version 5.x (McCollister Branding)				#
#=======================================================================#
$(MAIN_PRC_5): $(OBJ_MAIN_5) $(OBJ_MAIN_RO_5)
	$(BUILDPRC) $(BUILDPRCFLAGS) -o $@ -n $(ICON_TEXT) \
		-c $(APP_ID) -t $(APP_TYPE_5) $(OBJ_MAIN_RO_5) $(OBJ_MAIN_5)
	$(DIR) $@

$(OBJ_MAIN_RO_5): $(MAIN_RCP) $(MAIN_H) $(MAIN_V_PRC_5)
	$(PILRC) -D BRAND_M $(PILRCFLAGS) -D STANDALONE -ro \
		-o $@ $(MAIN_RCP)

#=======================================================================#
#       Version 5.x (PalmGear Branding)					#
#=======================================================================#
$(MAIN_PRC_5_P): $(OBJ_MAIN_5) $(OBJ_MAIN_RO_5_P)
	$(BUILDPRC) $(BUILDPRCFLAGS) -o $@ -n $(ICON_TEXT) \
		-c $(APP_ID) -t $(APP_TYPE_5) $(OBJ_MAIN_RO_5_P) $(OBJ_MAIN_5)
	$(DIR) $@

$(OBJ_MAIN_RO_5_P): $(MAIN_RCP) $(MAIN_H) $(MAIN_V_PRC_5)
	$(PILRC) -D BRAND_P $(PILRCFLAGS) -D STANDALONE -ro \
		-o $@ $(MAIN_RCP)

#=======================================================================#
#       Version 4.x & 5.x README.htm (McCollister Branding)		#
#=======================================================================#
$(README_HTM): $(README_GENERIC_HTM)
	$(SED) 's/_RegLinkText_/$(REG_LINK_TEXT)/g' $< > $(README_TMP)
	$(SED) 's/_RegLink_/$(REG_LINK)/g' $(README_TMP) > $@
	$(RM) $(README_TMP)

#=======================================================================#
#       Version 4.x & 5.x README.htm (PalmGear Branding)		#
#=======================================================================#
$(README_HTM_P): $(README_GENERIC_HTM)
	$(SED) 's/_RegLinkText_/$(REG_LINK_TEXT_P)/g' $< > $(README_TMP)
	$(SED) 's/_RegLink_/$(REG_LINK_P)/g' $(README_TMP) > $@
	$(RM) $(README_TMP)

#=======================================================================#
#       Maintenance and Cleanup						#
#=======================================================================#
clean_obj:
	$(RM) $(OBJ_MAIN_V)
	$(RM) $(MAIN_V_PRC)
	($(CD) $(OBJ_DIR)   ; $(RM) $(MAIN) $(MAIN_RO) $(MAIN_RO_P) \
		$(COMMON)_??.o $(COMMON_LIB) *.o)
	$(RM) $(OBJ_MAIN_V_5)
	$(RM) $(MAIN_V_PRC_5)
	($(CD) $(OBJ_DIR_5) ; $(RM) $(MAIN) $(MAIN_RO) $(MAIN_RO_P) \
		$(COMMON)_??.o $(COMMON_LIB) *.o)

tidy: clean_obj
	$(RM) $(DIST_ZIP)
	$(RM) $(DIST_ZIP_P)
	$(RM) $(DIST_SRC_ZIP)

clean: tidy
	$(RM) $(MAIN_PRC)
	$(RM) $(MAIN_PRC_5)
	$(RM) $(README_HTM)
	$(RM) $(MAIN_PRC_P)
	$(RM) $(MAIN_PRC_5_P)
	$(RM) $(README_HTM_P)

force: clean all

dist: all clean_obj
	$(ZIP)     $(DIST_ZIP)     $(DIST_FILES)
	$(ZIP)     $(DIST_ZIP_P)   $(DIST_FILES_P)
	$(ZIP_ALL) $(DIST_SRC_ZIP) $(DIST_ALL)
