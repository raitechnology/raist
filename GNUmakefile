# defines a directory for build, for example, RH6_x86_64
lsb_dist     := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -is ; else echo Linux ; fi)
lsb_dist_ver := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
uname_m      := $(shell uname -m)

short_dist_lc := $(patsubst CentOS,rh,$(patsubst RedHatEnterprise,rh,\
                   $(patsubst RedHat,rh,\
                     $(patsubst Fedora,fc,$(patsubst Ubuntu,ub,\
                       $(patsubst Debian,deb,$(patsubst SUSE,ss,$(lsb_dist))))))))
short_dist    := $(shell echo $(short_dist_lc) | tr a-z A-Z)
pwd           := $(shell pwd)
rpm_os        := $(short_dist_lc)$(lsb_dist_ver).$(uname_m)

# this is where the targets are compiled
build_dir ?= $(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)
bind      := $(build_dir)/bin
libd      := $(build_dir)/lib64
objd      := $(build_dir)/obj
dependd   := $(build_dir)/dep

# use 'make port_extra=-g' for debug build
ifeq (-g,$(findstring -g,$(port_extra)))
  DEBUG = true
endif

CC          ?= gcc
CXX         ?= g++
cc          := $(CC)
cpp         := $(CXX)
# if not linking libstdc++
ifdef NO_STL
cppflags    := -std=c++11 -fno-rtti -fno-exceptions
cpplink     := $(CC)
else
cppflags    := -std=c++11
cpplink     := $(CXX)
endif
arch_cflags := -mavx -maes -fno-omit-frame-pointer
gcc_wflags  := -Wall -Wextra -Werror
fpicflags   := -fPIC
soflag      := -shared

ifdef DEBUG
default_cflags := -ggdb
else
default_cflags := -ggdb -O3 -Ofast
endif
# rpmbuild uses RPM_OPT_FLAGS
CFLAGS := $(default_cflags)
#RPM_OPT_FLAGS ?= $(default_cflags)
#CFLAGS ?= $(RPM_OPT_FLAGS)
cflags := $(gcc_wflags) $(CFLAGS) $(arch_cflags)

# where to find the raids/xyz.h files
INCLUDES    ?= -Iinclude -Iraikv/include -Iraimd/include
includes    := $(INCLUDES)
DEFINES     ?=
defines     := $(DEFINES)
cpp_lnk     :=
sock_lib    :=
math_lib    := -lm
thread_lib  := -pthread -lrt

# test submodules exist (they don't exist for dist_rpm, dist_dpkg targets)
have_md_submodule    := $(shell if [ -f ./raimd/GNUmakefile ]; then echo yes; else echo no; fi )
have_dec_submodule   := $(shell if [ -f ./raimd/libdecnumber/GNUmakefile ]; then echo yes; else echo no; fi )
have_kv_submodule    := $(shell if [ -f ./raikv/GNUmakefile ]; then echo yes; else echo no; fi )

lnk_lib     :=
dlnk_lib    :=
lnk_dep     :=
dlnk_dep    :=

# if building submodules, reference them rather than the libs installed
ifeq (yes,$(have_kv_submodule))
kv_lib      := raikv/$(libd)/libraikv.a
kv_dll      := raikv/$(libd)/libraikv.so
lnk_lib     += $(kv_lib)
lnk_dep     += $(kv_lib)
dlnk_lib    += -Lraikv/$(libd) -lraikv
dlnk_dep    += $(kv_dll)
rpath1       = ,-rpath,$(pwd)/raikv/$(libd)
else
lnk_lib     += -lraikv
dlnk_lib    += -lraikv
endif

ifeq (yes,$(have_md_submodule))
md_lib      := raimd/$(libd)/libraimd.a
md_dll      := raimd/$(libd)/libraimd.so
lnk_lib     += $(md_lib)
lnk_dep     += $(md_lib)
dlnk_lib    += -Lraimd/$(libd) -lraimd
dlnk_dep    += $(md_dll)
rpath3       = ,-rpath,$(pwd)/raimd/$(libd)
else
lnk_lib     += -lraimd
dlnk_lib    += -lraimd
endif

ifeq (yes,$(have_dec_submodule))
dec_lib     := raimd/libdecnumber/$(libd)/libdecnumber.a
dec_dll     := raimd/libdecnumber/$(libd)/libdecnumber.so
lnk_lib     += $(dec_lib)
lnk_dep     += $(dec_lib)
dlnk_lib    += -Lraimd/libdecnumber/$(libd) -ldecnumber
dlnk_dep    += $(dec_dll)
rpath5       = ,-rpath,$(pwd)/raimd/libdecnumber/$(libd)
else
lnk_lib     += -ldecnumber
dlnk_lib    += -ldecnumber
endif

raist_lib := $(libd)/libraist.a
rpath       := -Wl,-rpath,$(pwd)/$(libd)$(rpath1)$(rpath2)$(rpath3)$(rpath4)$(rpath5)$(rpath6)$(rpath7)
dlnk_lib    += -lpcre2-8 -lcrypto
malloc_lib  :=

.PHONY: everything
everything: $(kv_lib) $(dec_lib) $(md_lib) $(raist_lib) all

clean_subs :=
dlnk_dll_depend :=
dlnk_lib_depend :=

# build submodules if have them
ifeq (yes,$(have_kv_submodule))
$(kv_lib) $(kv_dll):
	$(MAKE) -C raikv
.PHONY: clean_kv
clean_kv:
	$(MAKE) -C raikv clean
clean_subs += clean_kv
endif
ifeq (yes,$(have_dec_submodule))
$(dec_lib) $(dec_dll):
	$(MAKE) -C raimd/libdecnumber
.PHONY: clean_dec
clean_dec:
	$(MAKE) -C raimd/libdecnumber clean
clean_subs += clean_dec
endif
ifeq (yes,$(have_md_submodule))
$(md_lib) $(md_dll):
	$(MAKE) -C raimd
.PHONY: clean_md
clean_md:
	$(MAKE) -C raimd clean
clean_subs += clean_md
endif

# copr/fedora build (with version env vars)
# copr uses this to generate a source rpm with the srpm target
-include .copr/Makefile

# debian build (debuild)
# target for building installable deb: dist_dpkg
-include deb/Makefile

# targets filled in below
all_exes    :=
all_libs    :=
all_dlls    :=
all_depends :=
gen_files   :=

libraist_files := ev_gc
libraist_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(libraist_files)))
libraist_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libraist_files)))
libraist_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(libraist_files))) \
                  $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libraist_files)))
libraist_dlnk  := $(dlnk_lib)
libraist_spec  := $(version)-$(build_num)
libraist_ver   := $(major_num).$(minor_num)

$(libd)/libraist.a: $(libraist_objs)
$(libd)/libraist.so: $(libraist_dbjs) $(dlnk_dep)

all_libs    += $(libd)/libraist.a
all_dlls    += $(libd)/libraist.so
all_depends += $(libraist_deps)

server_defines := -DRAIDS_VER=$(ver_build)
gc_server_files := server
gc_server_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(gc_server_files)))
gc_server_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(gc_server_files)))
gc_server_libs  := $(raist_lib)
gc_server_lnk   := $(raist_lib) $(lnk_lib) -lpcre2-8

$(bind)/gc_server: $(gc_server_objs) $(gc_server_libs) $(lnk_dep)

all_exes    += $(bind)/gc_server
all_depends += $(gc_server_deps)

all_dirs := $(bind) $(libd) $(objd) $(dependd)

# the default targets
.PHONY: all
all: $(all_libs) $(all_dlls) $(all_exes)

.PHONY: dnf_depend
dnf_depend:
	sudo dnf -y install make gcc-c++ git redhat-lsb openssl-devel pcre2-devel chrpath

.PHONY: yum_depend
yum_depend:
	sudo yum -y install make gcc-c++ git redhat-lsb openssl-devel pcre2-devel chrpath

.PHONY: deb_depend
deb_depend:
	sudo apt-get install -y install make g++ gcc devscripts libpcre2-dev chrpath git lsb-release libssl-dev

# create directories
$(dependd):
	@mkdir -p $(all_dirs)

# remove target bins, objs, depends
.PHONY: clean
clean: $(clean_subs)
	rm -r -f $(bind) $(libd) $(objd) $(dependd)
	if [ "$(build_dir)" != "." ] ; then rmdir $(build_dir) ; fi

.PHONY: clean_dist
clean_dist:
	rm -rf dpkgbuild rpmbuild

.PHONY: clean_all
clean_all: clean clean_dist

# force a remake of depend using 'make -B depend'
.PHONY: depend
depend: $(dependd)/depend.make

$(dependd)/depend.make: $(dependd) $(all_depends)
	@echo "# depend file" > $(dependd)/depend.make
	@cat $(all_depends) >> $(dependd)/depend.make

.PHONY: dist_bins
dist_bins: $(all_libs) $(all_dlls) $(bind)/gc_server
	chrpath -d $(libd)/libraist.so
	chrpath -d $(bind)/gc_server

.PHONY: dist_rpm
dist_rpm: srpm
	( cd rpmbuild && rpmbuild --define "-topdir `pwd`" -ba SPECS/raist.spec )

# dependencies made by 'make depend'
-include $(dependd)/depend.make

ifeq ($(DESTDIR),)
# 'sudo make install' puts things in /usr/local/lib, /usr/local/include
install_prefix = /usr/local
else
# debuild uses DESTDIR to put things into debian/raist/usr
install_prefix = $(DESTDIR)/usr
endif

install: dist_bins
	install -d $(install_prefix)/lib $(install_prefix)/bin
	install -d $(install_prefix)/include/raist
	for f in $(libd)/libraist.* ; do \
	if [ -h $$f ] ; then \
	cp -a $$f $(install_prefix)/lib ; \
	else \
	install $$f $(install_prefix)/lib ; \
	fi ; \
	done
	install -m 644 include/raist/*.h $(install_prefix)/include/raist

$(objd)/%.o: src/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/%.c
	$(cc) $(cflags) $(fpicflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: test/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: test/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(libd)/%.a:
	ar rc $@ $($(*)_objs)

$(libd)/%.so:
	$(cpplink) $(soflag) $(rpath) $(cflags) -o $@.$($(*)_spec) -Wl,-soname=$(@F).$($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(cpp_dll_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_spec) $(@F).$($(*)_ver) && ln -f -s $(@F).$($(*)_ver) $(@F)

$(bind)/%:
	$(cpplink) $(cflags) $(rpath) -o $@ $($(*)_objs) -L$(libd) $($(*)_lnk) $(cpp_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(bind)/%.static:
	$(cpplink) $(cflags) -o $@ $($(*)_objs) $($(*)_static_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(dependd)/%.d: src/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: src/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.d: test/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: test/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

