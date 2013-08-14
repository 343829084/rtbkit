# RTBKIT router makefile

LIBRTB_ROUTER_SOURCES := \
	augmentation_loop.cc \
	router.cc \
	router_types.cc \
	router_stack.cc \
	filter_pool.cc

LIBRTB_ROUTER_LINK := \
	rtb zeromq boost_thread logger opstats crypto++ leveldb gc services redis banker agent_configuration monitor monitor_service post_auction filter_registry

$(eval $(call library,rtb_router,$(LIBRTB_ROUTER_SOURCES),$(LIBRTB_ROUTER_LINK)))

$(eval $(call program,router_runner,rtb_router boost_program_options))

$(eval $(call include_sub_make,rtb_router_testing,testing,rtb_router_testing.mk))
