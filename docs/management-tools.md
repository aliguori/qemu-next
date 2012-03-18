Writing a QEMU Management Tool
==============================

This is a guide on writing a QEMU management tool.  This document outlines
which interfaces the QEMU project exports and will support long term.

Specifying options
------------------

The preferred way for a management tool to specify options to QEMU is to create
a configuration file and use '-readconfig' on the command line.  We prefer this
interface over direct use of command line options because this interface
supports introspection in a machine friendly way.

Determining if an option is supported
-------------------------------------

A management tool should use the '-query-capabilities' option to determine what
the capabilities of the current QEMU executable is.  The 'config' section of
the resulting JSON object describes all of the config sections that are
currently supported.

Many options currently fall into the 'system' section and some of these options
have complex syntax.  Management tools should assume that these syntaxes will
never change and never be extended.  Whenever there is a need to change or
extend this syntax, we will first convert the option to use a stand alone
section.

While help text is available, the help text *should not* be parsed to attempt to
determine if the option has changed.  The help text is exposed merely as a stop
gap to help libvirt transition to this new format.
