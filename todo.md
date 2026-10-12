**I’d build a small opponent response model next, then connect it to simulations that use your existing ICM calculator.** We don’t need a large AI system yet.

Here’s the order I recommend:

1. **Give the AI the information it needs.**  
   Collect public actions: who checked, bet, called, raised, or folded, and the amounts. Include tournament prizes, remaining players, and blinds in its decision context. It should also see its own cards—but never opponents’ actual hidden cards.

2. **Create a few simple opponent behaviours.**  
   Start with cautious, aggressive, and frequent-calling opponents. Each should return probabilities for legal actions rather than predict one action with certainty. For example: “Against this bet, this opponent folds 50%, calls 40%, and raises 10%.”

   Initially, these are configurable assumptions. Later, we can adjust them using observed behaviour. Separating uncertainty about opponents’ cards from uncertainty about their strategy is also the approach explored in [Bayes’ Bluff](https://poker.cs.ualberta.ca/publications/UAI05.pdf).

3. **Test the response model before trusting it.**  
   Run games against those known behaviours and check whether predicted frequencies match actual responses. Also test that simulated players make decisions using their own cards and public information. This gives us a controlled foundation before trying to model humans.

4. **Build one small ICM decision experiment.**  
   Start with a tournament situation where you face an all-in and can **call or fold**, with at least three players still in the tournament.

   For each choice:
   - Sample plausible opponent cards and any missing board cards.
   - Settle the hand using the engine’s existing pot logic.
   - Calculate your value afterward: ICM if you survive, your actual finishing prize if you are eliminated.
   - Average those values and compare call against fold.

   This tests the entire connection between card sampling, payouts, elimination, and ICM without needing to guess many future actions.

5. **Then expand to raises and continued play.**  
   Compare fold, call, a small raise, a large raise, and all-in where legal. Use the response model to continue each hypothetical hand until it ends, then score the outcome. Test the resulting AI against several fixed opponent styles—not just the style it was developed against.

**The next concrete coding milestone should be public action tracking and a simple configurable response model.** After that, the call-or-fold experiment will give us a manageable first AI decision tool.

I would postpone transformers, genetic algorithms, and a large learning system until we can measure whether this simpler approach makes better decisions. Also, more simulations reduce sampling noise; they don’t fix incorrect assumptions about opponents.
