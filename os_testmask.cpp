/*
 * OperServ TESTMASK
 *
 * Copyright (C) 2023 - David Schultz (me@zpld.me)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Allows Opers to be view users affected by an AKILL
 * Configuration to put into your operserv config:
module { name = "os_testmask" }
command { service = "OperServ"; name = "TESTMASK"; command = "operserv/testmask"; permission = "operserv/testmask"; }
 */

#include "module.h"

static ServiceReference<XLineManager> akills("XLineManager", "xlinemanager/sgline");

class CommandOSTestMask : public Command
{
public:
	CommandOSTestMask(Module *creator) : Command(creator, "operserv/testmask", 1, 1)
	{
		this->SetDesc(_("Get number of users affected by an AKILL"));
		this->SetSyntax(_("\037mask\037"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (params.empty()) {
			this->OnSyntaxError(source, "<mask>");
			return;
		}

		Anope::string mask = params[0];
		if (mask.find('@') == Anope::string::npos)
		{
			source.Reply(BAD_USERHOST_MASK);
			return;
		}

		XLine *x = new XLine(mask, source.GetNick(), 0, ""); // dummy XLine
		unsigned int affected = 0;
		for (user_map::const_iterator it = UserListByNick.begin(); it != UserListByNick.end(); ++it)
			if (akills->Check(it->second, x))
				++affected;
		source.Reply(_("Mask \002%s\002 affects %d users"), x->mask.c_str(), affected);
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		return;
	}
};

class ModuleTestMask : public Module
{
	CommandOSTestMask commandostestmask;
public:
	ModuleTestMask(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		commandostestmask(this)
	{
		this->SetAuthor("launchd");
		this->SetVersion("0.1");
	}
};

MODULE_INIT(ModuleTestMask)
