TOPTARGETS := all clean install uninstall
SUBDIRS := dotdetect dotblur rainbowdetect

$(TOPTARGETS): $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY:	$(TOPTARGETS) $(SUBDIRS)
