TEMPLATE = subdirs

# to make the file visible in qt creator
# OTHER_FILES += common.pri

SUBDIRS = \
    CartaLib \
    core \
    desktop \
    plugins \
    Tests \
    testCache \
#    testRegion \
#    testPercentile

# explicit dependencies, to make sure parallel make works (i.e. make -j4...)
core.depends = CartaLib
desktop.depends = core
testRegion.depends = core
plugins.depends = core
testRegion.depends = core
testCache.depends = core
testPercentile.depends = core
Tests.depends = core desktop plugins

# ... or ...
# build directories in order, or make sure to update dependencies manually, or make -j4 won't work
# ordered is the slowest option but most reliable and requires no maintenance :)
#CONFIG += ordered
