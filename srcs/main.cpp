/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/09/08 23:12:11 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "App.hpp"
#include <iostream>
#include <vector>
#include <memory>

int main(void)
{
	std::map<settings_t, bool> settings = {{VOX_FULLSCREEN, false}, {VOX_RESIZE, false}, {VOX_DECORATED, true}, {VOX_HEADLESS, false}};

	std::unique_ptr<App> app(new App(1440, 900, "Hello World", settings));
	// std::unique_ptr<App> app(new App(800, 600, "Hello World", settings));
	if (!app.get())
		return (EXIT_FAILURE);
	
	app->run();
	app->terminate();

	return (EXIT_SUCCESS);
}