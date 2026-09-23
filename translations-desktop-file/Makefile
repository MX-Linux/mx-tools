#!/usr/bin/make -f

PKGNAM := $(basename $(dirname $(pwd)))-desktop

BUILD  := ./build
PO_DIR := ./po
XDGDIR := ./desktop-in

DESKTOP_IN := $(wildcard $(XDGDIR)/*.desktop.in)
DESKTOP    := $(addprefix $(BUILD)/,$(notdir $(basename $(DESKTOP_IN))))

ifneq ($(strip $(LINGUAS)),)
  LINGUAS := $(sort $(LINGUAS))
  $(file >$(PO_DIR)/LINGUAS)
  $(foreach po,$(LINGUAS),$(file >>$(PO_DIR)/LINGUAS,$(po)))
else ifneq ($(wildcard $(PO_DIR)/LINGUAS),)
  LINGUAS := $(sort $(strip $(shell grep '^[[:space:]]*[a-z]' $(PO_DIR)/LINGUAS)))
else
  LINGUAS := $(sort $(basename $(notdir $(wildcard $(PO_DIR)/*.po))))
  $(foreach po,$(LINGUAS),$(file >>$(PO_DIR)/LINGUAS,$(po)))
endif

all: clean desktop

locale: $(LOCALE)

$(BUILD)/locale/%/$(LCMSGS): $(PO_DIR)/%.po
	@mkdir -p $(BUILD)/locale/$*/LC_MESSAGES
	@echo "  " $@
	@msgfmt $< -o $@

desktop: $(DESKTOP)

$(BUILD)/%.desktop: $(XDGDIR)/%.desktop.in
	@mkdir -p $(BUILD)/
	@echo "  " $@
	msgfmt --desktop --template $< -d $(PO_DIR) -o $@
	@sed -i '/^#/d' $@
	@sed -i 's/[^\[_]*_//' $@
	@cp $(BUILD)/*.desktop ../

clean:
	-@rm  -f $(DESKTOP)
	-@rm -rf $(BUILD)

.PHONY: all clean desktop
