/*
 * OperServ Notify
 *
 * Copyright (C) 2023 - David Schultz (me@zpld.me)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Allows Opers to be notified of channels being joined at an abnormally high rate
 * Configuration to put into your operserv config:
module { name = "os_joinrate" }
command { service = "OperServ"; name = "JOINRATE"; command = "operserv/joinrate"; permission = "operserv/joinrate"; }
 */

#include "module.h"

struct JRConfig;
struct JRBucket;

static Serialize::Checker<std::vector<JRConfig *> > configs("JRConfig");
static JRConfig *default_config;
static Anope::hash_map<JRBucket *> buckets;

struct JRBucket
{
	int tokens;
	time_t last_join_time;
	time_t last_warn_time;

	JRBucket(const Anope::string &c) : tokens(-1), last_join_time(Anope::CurTime), last_warn_time(0)
	{
		buckets[c] = this;
	}

	static JRBucket *Find(Anope::string c)
	{
		c = c.lower();
		Anope::hash_map<JRBucket *>::iterator it = buckets.find(c);
		if (it != buckets.end())
			return it->second;
		return NULL;
	}

	static JRBucket *FindOrCreate(Channel *chan)
	{
		Anope::string c = chan->name.lower();
		JRBucket *bucket = JRBucket::Find(c);
		if (!bucket)
			return new JRBucket(c);
		return bucket;
	}
};

struct JRConfig : Serializable
{
	Anope::string chname;
	int rate;
	int time;

	JRConfig() : Serializable("JRConfig")
	{
		configs->push_back(this);
	}
	~JRConfig()
	{
		std::vector<JRConfig *>::iterator it = std::find(configs->begin(), configs->end(), this);
		if (it != configs->end())
			configs->erase(it);
	}

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["chname"] << chname;
		data["rate"] << rate;
		data["time"] << time;
	}

	static Serializable *Unserialize(Serializable *obj, Serialize::Data &data)
	{
		JRConfig *config;

		if (obj)
			config = anope_dynamic_static_cast<JRConfig *>(obj);
		else
			config = new JRConfig();

		data["chname"] >> config->chname;
		data["rate"] >> config->rate;
		data["time"] >> config->time;

		return config;
	}

	static JRConfig *Find(const Anope::string &chname)
	{
		JRConfig *config;
		for (unsigned i = 0; i < configs->size(); ++i)
		{
			config = configs->at(i);
			if (config->chname.equals_ci(chname))
				return config;
		}

		return NULL;
	}
};

class CommandOSJoinRate : public Command
{
	void GetRate(CommandSource &source, const std::vector<Anope::string> &params)
	{
		JRConfig *jc = JRConfig::Find(params[1]);
		if (!jc)
		{
			source.Reply(_("Joinrate warning threshold for %s is set to %d joins in %ds (default)"), params[1].c_str(), default_config->rate, default_config->time);
			return;
		}

		source.Reply(_("Joinrate warning threshold for %s is set to %d joins in %ds"), params[1].c_str(), jc->rate, jc->time);
	}

	void SetRate(CommandSource &source, const std::vector<Anope::string> &params)
	{
		JRConfig *jc = JRConfig::Find(params[1]);
		JRBucket *bucket = JRBucket::Find(params[1]);

		if (params[2].equals_ci("DEFAULT"))
		{
			if (jc)
				delete jc;
			if (bucket)
				bucket->tokens = -1;
			source.Reply(_("%s has been returned to the default warning threshold"), params[1].c_str());
			return;
		}
		if (params.size() < 4)
		{
			source.Reply(_("Please provide a rate and time limit"));
			return;
		}

		int rate;
		int time;
		try
		{
			rate = convertTo<int>(params[2]);
			if (params.size() >= 4)
				time = convertTo<int>(params[3]);
		}
		catch (const ConvertException &)
		{
			source.Reply("Invalid value given for rate or time.");
			return;
		}
		if (!jc)
			jc = new JRConfig();
		jc->chname = params[1];
		jc->rate = rate;
		jc->time = time;

		if (bucket)
			bucket->tokens = -1;

		source.Reply(_("Joinrate warning threshold for %s is now set to %d joins in %ds"), params[1].c_str(), jc->rate, jc->time);
	}
public:
	CommandOSJoinRate(Module *creator) : Command(creator, "operserv/joinrate", 2, 4)
	{
		this->SetDesc(_("Configure join rate thresholds for channels"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (params.empty())
			this->OnSyntaxError(source, "<GET|SET> <#channel|DEFAULT> <joins> <seconds>");
		else if (params[0].equals_ci("GET") && params.size() > 1)
			this->GetRate(source, params);
		else if (params[0].equals_ci("SET") && params.size() > 2)
			this->SetRate(source, params);
		else
			this->OnSyntaxError(source, "<GET|SET> <#channel|DEFAULT> <joins> <seconds>");
	}
};

class ModuleJoinRate : public Module
{
	Serialize::Type jrconfig_type;
	CommandOSJoinRate commandosjoinrate;
	bool ready;

	void InitializeDefaultConfig()
	{
		default_config = JRConfig::Find("DEFAULT");
		if (default_config == NULL)
		{
			// initialize a default config on first run
			Log(this) << "Default configuration has not been initialized. Let's initialize!";
			default_config = new JRConfig();
			default_config->chname = "DEFAULT";
			default_config->rate = 5;
			default_config->time = 5;
		}
		ready = true;
	}
public:
	ModuleJoinRate(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		jrconfig_type("JRConfig", JRConfig::Unserialize), commandosjoinrate(this)
	{
		this->SetAuthor("launchd");
		this->SetVersion("0.1");
		ready = false;

		if ((Me && Me->IsSynced()) && !ready)
			InitializeDefaultConfig();
	}

	void OnPostInit() anope_override
	{
		// this is a hack to escape a race condition where the default_config exists but has not been found in the db
		// there's gotta be a better way to do this
		if (ready)
			return;
		InitializeDefaultConfig();
	}

	void OnJoinChannel(User *u, Channel *c) anope_override
	{
		// now this is where the magic happens

		if (!c)
			return; // oops?

		// don't count JOINs during a netjoin
		if (Me && !Me->IsSynced())
			return;
		// U-Lined clients probably aren't who we're worried about on this
		if (u->server->IsULined() || !u->server->IsSynced())
			return;

		JRConfig *cn = JRConfig::Find(c->name);
		if (!cn)
			cn = default_config;

		// JOINRATE is disabled here
		if (cn->rate < 0 || cn->time < 0)
			return;

		JRBucket *bucket = JRBucket::FindOrCreate(c);
		if (bucket->tokens == -1)
			bucket->tokens = cn->rate;

		const time_t elapsed = (Anope::CurTime - bucket->last_join_time);
		bucket->tokens += (elapsed * cn->rate) / cn->time;

		if (bucket->tokens > cn->rate)
			bucket->tokens = cn->rate;

		if (bucket->tokens > 0)
		{
			bucket->tokens--;
		} else if ((Anope::CurTime - bucket->last_warn_time) >= 30) {
			bucket->last_warn_time = Anope::CurTime;
			Log(Config->GetClient("OperServ"), "joinrate/warn") << "JOINRATE: " << c->name << " exceeds warning threshold (" << cn->rate << " joins in " << cn->time << "s)";
		}

		bucket->last_join_time = Anope::CurTime;
	}
};

MODULE_INIT(ModuleJoinRate)
