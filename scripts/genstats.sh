#!/bin/bash

# Usage: scripts/genstats.sh "today" "1 year ago"

aliases="scripts/aliases.txt"
companies="scripts/companies.txt"

function dedup() {
    while read addr; do
	f=`grep "^$addr: " "$aliases" | cut -f2- -d' '`
	if test "$f"; then
	    echo "$f"
	else
	    echo "$addr"
	fi
    done
}

function gen-committers() {
    until="$1"
    since="$2"

    git log --until="$until" --since="$since" --pretty=format:%ce | \
	sort -u | dedup | sort -u | while read committer; do
	addresses=`grep " $committer\$" "$aliases" | cut -f1 -d: | while read a; do echo -n "--committer=$a "; done`
	
	echo -n "$committer, "
	git log --until="$until" --since="$since" \
	    --pretty=oneline --committer="$committer" $addresses | wc -l
    done
}

function gen-authors() {
    until="$1"
    since="$2"

    git log --until="$until" --since="$since" --pretty=format:%ae | \
	sort -u | dedup | sort -u | while read author; do
	addresses=`grep " $author\$" "$aliases" | cut -f1 -d: | while read a; do echo -n "--author=$a "; done`
	
	echo -n "$author, "
	git log --until="$until" --since="$since" \
	    --pretty=oneline --author="$author" | wc -l
    done
}

function gen-commits() {
    until="$1"
    since="$2"

    git log --until="$until" --since="$since" --pretty=oneline | wc -l
}

function gen-companies() {
    until="$1"
    since="$2"

    cat "$companies" | while read LINE; do
	company=`echo $LINE | cut -f1 -d:`
	addrs=`echo $LINE | cut -f2- -d:`

	authors=`echo "$addrs" | sed -e 's: : --author=:g'`
	echo "$company," \
	    `git log --until="$until" --since="$since" --pretty=oneline \
	         $authors | wc -l`, \
            `git log --until="$until" --since="$since" --pretty="format:%ae\n" \
	         $authors | sort -u | dedup | sort -u | wc -l`
    done
}

function gen-diffstat() {
    until="$1"
    since="$2"

    start=`git log --until="$until" --format="%H" | head -1`
    end=`git log --since="$since" --format="%H" | tail -1`
    stat=`git diffstat "$end" "$start" | tail -1`

    add=`echo $stat | cut -f4 -d' '`
    del=`echo $stat | cut -f6 -d' '`

    echo "add, $add"
    echo "delete, $del"
}

function gen-stats() {
    until="$1"
    since="$2"

    echo 'Total Commits'
    echo '-------------'
    gen-commits "$until" "$since"
    echo

    echo 'Committers'
    echo '----------'
    gen-committers "$until" "$since"
    echo

    echo 'Authors'
    echo '-------'
    gen-authors "$until" "$since"
    echo

    echo 'Companies'
    echo '---------'
    gen-companies "$until" "$since"
    echo

    echo 'Changes'
    echo '-------'
    gen-diffstat "$1" "$2"
}

gen-stats "$1" "$2"

