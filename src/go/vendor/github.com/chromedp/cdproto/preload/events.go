package preload

// Code generated by cdproto-gen. DO NOT EDIT.

// EventRuleSetUpdated upsert. Currently, it is only emitted when a rule set
// added.
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Preload#event-ruleSetUpdated
type EventRuleSetUpdated struct {
	RuleSet *RuleSet `json:"ruleSet"`
}

// EventRuleSetRemoved [no description].
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Preload#event-ruleSetRemoved
type EventRuleSetRemoved struct {
	ID RuleSetID `json:"id"`
}
