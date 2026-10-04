&#35; HTML-escaped Markdown

This file came out of an HTML pipeline: every Markdown character is a
character reference. mdt decodes it before parsing.

Some &#42;&#42;bold&#42;&#42;, some &#42;italic&#42;, some &#95;underlined&#95; text,
a &#91;link&#93;&#40;https://example.com&#41; and inline &#96;code&#96;.

&#45; first item
&#45; second item

&#124; a &#124; b &#124;
&#124;&#45;&#45;&#45;&#124;&#45;&#45;&#45;&#124;
&#124; 1 &#124; 2 &#124;

&#96;&#96;&#96;c
&#x09;int x = 1; /* tab above */
&#x09;if (x &amp;&amp; y) return 0;
&#96;&#96;&#96;
