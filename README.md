feel free to send suggestions at:
yardenks@gmail.com

### note:
gonna be honest, the first part of this file was obviously written by AI (like most readmes...). If 
you made it here, I want you to have an authentic experience, I want you to read my ideas the way I see them,
therefore, I'm gonna seperate this readme into 2 parts, first part written by AI, the "proffesional" readme,
the part where you gonna find how to run this code and so on... on the second part that will be after the "not written by AI" title
I will include my own personal opinions and thoughts about this project, where Im planning on taking it and further steps I want to make.
If you're interested in poker bots and poker in general, if you're interested in how to make a poker simulator, tournament and some
analysis tools and AI bot making, you might enjoy this repo.

#### by the way, please send sugestions if you have some, I WILL read them.

# poker simulator++

A fast C++ Texas Hold'em engine for interactive play, large-scale simulations,
and poker AI research.

- No-limit Hold'em for 2–8 players, with side pots and split payouts.
- Visual terminal play against simple AI opponents.
- Fast hand evaluation using precomputed lookup tables.
- Reproducible simulations, parallel workers, snapshots, and batched decisions.
- Single-table tournament API with rising blinds, tied finishes, and prize payouts.

## Build

Requires a C++20 compiler and CMake. Run in Linux or Ubuntu/WSL:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Play

```bash
./build/poker_cli
```

The default mode is a freezeout tournament with increasing blinds and finishing-place
prizes. Use `./build/poker_cli --game` for ordinary fixed-blind play.
Choose a player count. You play seat 0. Commands: `fold`, `check`, `call`,
`raise 500`, and `allin`. Raise amounts are totals for the current betting round.

## Simulate

```bash
cmake --build build --target poker_tournament_simulation -j
./build/poker_tournament_simulation 10000 4
```

The arguments specify complete tournaments and worker threads. The runner reports
hands, decisions, wins, and total prizes per seat. Prizes are abstract integer units.
For ordinary hand/match experiments, build and run `poker_simulation_benchmark`.
The included AI is a simple baseline; it does not assess hand strength.

## Test

```bash
ctest --test-dir build --output-on-failure
```

See [SIMULATION_GUIDE.md](SIMULATION_GUIDE.md) for research APIs and supported
betting rules.

See [AI_RESEARCH_PLAN.md](AI_RESEARCH_PLAN.md) for the mathematical AI roadmap,
implementation milestones, and linked research papers and dissertations.

## not written by AI

so, first of all you will probably enjoy looking at the code as i made sure it has plenty of comments.
most of the code was written by AI but was carefully looked through and i really went over the code before 
i was satisfied with what's in this repo. so yeah even tho most of it is vibe coding (and not all!), i'm
sure you'll find value reading the code, if your goal is to study how to make a poker simulator and analysis tools
for poker, i believe this repo is valuable for you. take your time, i made sure this code is literally like a teacher
teaching you how to make a poker simulator. it doesn't matter where you start to read the code from, once you read enough
code you'll understand how things work.

second, at the current point in time I'm trying to develope a bot for the simulator.
why?
so i can beat my friends at our poker nights, which is a global public concern for sure.
I want you to know that I'm an undergraduate and currently doing CS degree. I'm just starting my machine learning course (haven't done any lecture at
the time of writing this) and i really hope for insights, but just know that everything here is made up from a personal research.

### my current plan

so really making a bot for poker is just make the bot make decisions that will make him make money overtime, so i want 
to maximize the expected cash income.
I obviously can make the AI choose randomly what action to make, but given even simple facts, for example if you have 2 and 6 in your hand, you probably
will benefit more from folding other than calling most times. 
from this idea a monte carlo algorithm is obvious. just run millions of games and see how many games your hand wins.
but there's a problem with this approach as a poker game isn't just a straight forward "the person with the strongest hand wins", people can bluff, in
theory 2 and 7 is worse than 2 and 6, someone with 2 and 7 can raise by a lot and folding for me is basically a loss, i have better chances of winning.
this is obvious, you probably know it and thought of it a long time ago, but it goes to show that there's something that you can do to improve
your expected income other than just guessing randomly what to do.
i made an analysis file, in it you can find some analysis tools that will eventually be used in the AI decision making.
these analysis tools are "mathematical" in sense that they rely on dry game facts.
i think there's to this game than just dry mathematical decisions.
just think of it, when you playing poker, you're looking at your opponents faces, one of your friends just raised like 5 big blinds for the past 10 hands 
before the flop, we all know that most chances he's bluffing. that's why i want to try and make a behavioral profile for each bot. let me explain:
so let's say bob is our bot. we can give bob some attributes like how aggressive he is, how conservative he is, maybe he bluff a lot, maybe not, there lots of 
things that can affect bob's behaviour. i want to make a behaviour profile that will contain such attributes, and let's say bob has 0.9 in the bluffing attribute,
then bob is likely to bluff a lot. also, bob can try to predict behavioral profiles of other players. if the community cards are 2, 7 and 8, and someone decide to 
go all in, what are the chances he really have a strong combination of cards? probably lower than if the community cards were K, Q and J, in the 2,7,8 i'd say
the person who went all in is most likely bluffing, so i'm going to increase his bluffing attribute.
you probably get the idea by now.
unfortunately I don't have much time to write this know, i have a lecture in few minutes so I'm going to stop here, but I'm gonna continue later for sure.
i have more ideas to come. if you're here and reading this stay tuned and send suggestions!
