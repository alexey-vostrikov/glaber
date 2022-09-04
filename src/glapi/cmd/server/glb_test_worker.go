package main

import (
	"fmt"
	"os"
	"math/rand"

)

func main() {
	template :=`Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam in lorem a ipsum gravida suscipit. Nunc tempus neque dui, a tincidunt tortor tincidunt`
	// nec. Sed maximus eget velit nec pellentesque. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Proin luctus massa massa, eleifend dignissim augue dictum in. Maecenas diam est, iaculis et vestibulum ut, feugiat eget augue. Morbi eget eros nec lacus ornare euismod sit amet et dolor. Duis nisi nunc, imperdiet ut gravida vitae, pellentesque tristique erat.	Phasellus eget nisl leo. Nunc eu tempor libero. Donec malesuada, libero sit amet egestas ornare, nisi tellus vulputate est, nec convallis velit dolor a nunc. Pellentesque mollis mauris quis lacinia pharetra. Pellentesque eget elementum nisl. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; Sed odio libero, interdum ut diam id, sollicitudin varius justo. Maecenas ac ipsum massa. Duis malesuada tortor id eros mollis porttitor. Curabitur pellentesque feugiat leo, eu tincidunt ipsum tristique non. Proin a lobortis nulla, posuere congue risus. Nunc felis purus, pharetra a tincidunt et, facilisis quis sem. Curabitur metus eros, vestibulum eu nisl ac, consequat iaculis arcu. Nunc pulvinar justo eget varius gravida. Cras non augue dolor.	Donec et lacus mattis ex facilisis facilisis. Quisque venenatis pulvinar pharetra. Integer dignissim euismod lorem nec consectetur. Phasellus placerat eros sed ipsum mattis, eu rutrum diam rhoncus. Nulla pretium nisi eu efficitur malesuada. Nulla facilisi. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Nam et pellentesque dui. Fusce maximus varius ex, facilisis fringilla nisl cursus et.	Proin turpis velit, congue et dolor sit amet, iaculis tempor est. Nulla sem augue, mollis quis fermentum vel, porta in enim. Proin quam lorem, euismod efficitur nisl id, vulputate fermentum odio. Mauris id nulla pretium, lobortis orci vel, euismod massa. Duis ut lacinia eros massa nunc.`

	for 1 == 1 {
		fmt.Fprintf(os.Stdout,"{\"someid:\":\"%d\", \"somevalue\":\"%s\"}\n",rand.Intn(1000), template)
	}
	
}



