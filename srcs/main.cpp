/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/08/29 10:36:42 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <glad/gl.h>
#include "App.hpp"
#include "Chunk.hpp"
#include "vertex.hpp"
#include <iostream>
#include <vector>
#include <memory>








int main(void)
{

	std::map<settings_t, bool> settings = {{VOX_FULLSCREEN, false}, {VOX_RESIZE, true}};

	std::unique_ptr<App> app(new App(1440, 900, "Hello World", settings));
	if (!app.get())
		return (EXIT_FAILURE);
	
	app->run();
	// app.get()->terminate();

	exit(EXIT_SUCCESS);
}